import unittest
import os
import base64
import http.client
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import time
from companion.codex_display.lan import records_state, aggregate, Rollouts, Store, handler, quota_metrics, task_states

def event(kind, **kw):
    return {'type':'event_msg','payload':{'type':kind,**kw}}
def item(kind, **kw):
    return {'type':'response_item','payload':{'type':kind,**kw}}

class StatusTests(unittest.TestCase):
    def test_lifecycle(self):
        self.assertEqual(records_state([]),'unknown')
        self.assertEqual(records_state([event('task_started')]),'running')
        self.assertEqual(records_state([event('task_started'),event('task_complete')]),'done')
        self.assertEqual(records_state([event('task_started'),event('turn_aborted')]),'idle')
    def test_async_question_remains_pending_until_user_reply(self):
        events=[event('task_started'),item('function_call', name='request_user_input_async', call_id='q'),
                item('function_call_output',call_id='q',output='{"accepted":true}')]
        self.assertEqual(records_state(events),'waiting')
        self.assertEqual(records_state(events+[event('user_message')]),'running')
    def test_approval_resolves_on_output(self):
        events=[event('task_started'),item('custom_tool_call',name='exec',call_id='a',input='sandbox_permissions: require_escalated')]
        self.assertEqual(records_state(events),'waiting')
        self.assertEqual(records_state(events+[item('custom_tool_call_output',call_id='a')]),'running')
    def test_large_rollout_keeps_running_and_consumes_appended_completion(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'rollout.jsonl'
            def line(record):
                return json.dumps(record).encode()+b'\n'
            path.write_bytes(line(event('task_started')) + line(item('message', text='x'*(5*1024*1024))))
            rollouts = Rollouts()
            self.assertEqual(rollouts.state(path), 'running')
            with path.open('ab') as f:
                f.write(line(event('task_complete')))
            self.assertEqual(rollouts.state(path), 'done')

    def test_parallel_tasks_include_recent_unindexed_session_and_dedupe_aliases(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def rollout(name, thread_id, source='vscode'):
                path = root / name
                rows = [{'type':'session_meta','payload':{'id':thread_id,'source':source}}, event('task_started')]
                path.write_text(''.join(json.dumps(row)+'\n' for row in rows), encoding='utf8')
                return path

            first = rollout('first.jsonl', 'first')
            duplicate = rollout('alias.jsonl', 'first')
            second = rollout('second.jsonl', 'second')
            child = rollout('child.jsonl', 'child', {'subagent': 'thread_spawn'})
            index = [{'id': 'first', 'path': str(root/'missing.jsonl'), 'status': {'type': 'notLoaded'}}]
            candidates = [first, duplicate, second, child]
            cache = Rollouts()
            self.assertEqual(aggregate(list(task_states(index, cache, candidates, time.time()))),
                             ('running', 2, 0))
            with second.open('a', encoding='utf8') as output:
                output.write(json.dumps(event('task_complete'))+'\n')
            self.assertEqual(aggregate(list(task_states(index, cache, candidates, time.time()))),
                             ('running', 1, 0))

    @unittest.skipUnless(os.name == 'nt', 'Windows byte-range lock')
    def test_quiet_task_counts_only_while_writer_holds_lock(self):
        import msvcrt
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            thread_id = '00000000-0000-4000-8000-000000000001'
            path = root/'session.jsonl'
            old = time.time() - 3600
            path.write_text(json.dumps(event('task_started', started_at=old))+'\n', encoding='utf8')
            os.utime(path, (old, old))
            thread = {'id': thread_id, 'path': str(path)}
            cache = Rollouts()
            def states():
                return list(task_states([thread], cache, [], time.time(), root))
            with (root/f'{thread_id}.lock').open('w+b', buffering=0) as lock:
                self.assertEqual(states(), ['unknown'])
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                try:
                    self.assertEqual(states(), ['idle'])
                    # Reopening writes settings and refreshes the log without a new turn.
                    with path.open('a', encoding='utf8') as output:
                        output.write(json.dumps(event('thread_settings_applied'))+'\n')
                    self.assertEqual(states(), ['idle'])
                    with path.open('a', encoding='utf8') as output:
                        output.write(json.dumps(event('task_started', started_at=time.time()))+'\n')
                    os.utime(path, (old, old))  # Exercise the quiet-turn path without waiting.
                    self.assertEqual(states(), ['running'])
                    with path.open('a', encoding='utf8') as output:
                        output.write(json.dumps(event('task_complete'))+'\n')
                    os.utime(path, (old, old))
                    self.assertEqual(states(), ['done'])
                finally:
                    msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)

    def test_priority(self):
        self.assertEqual(aggregate(['done','running','waiting']),('waiting',1,1))
        self.assertEqual(aggregate(['running','done']),('running',1,0))
        self.assertEqual(aggregate(['unknown','done'])[0],'done')
        self.assertEqual(aggregate(['unknown','idle'])[0],'idle')
        self.assertEqual(aggregate(['unknown'])[0],'unknown')

def window(minutes, used, reset=None):
    return {'windowDurationMins': minutes, 'usedPercent': used, 'resetsAt': reset}


class QuotaTests(unittest.TestCase):
    def test_weekly_and_five_hour_windows_in_either_order(self):
        week = window(10080, 1, 1800000000)
        short = window(300, 28, 1799500000)
        for primary, secondary in ((short, week), (week, short)):
            with self.subTest(primary=primary):
                result = quota_metrics({
                    'rateLimitsByLimitId': {'codex': {'primary': primary, 'secondary': secondary}},
                    'rateLimitResetCredits': {'availableCount': 1},
                })
                self.assertEqual((result['remaining'], result['reset_at']), (99, 1800000000))
                self.assertEqual((result['short_remaining'], result['short_reset_at']), (72, 1799500000))
                self.assertTrue(result['metrics_valid'])
                self.assertEqual(result['credits'], 1)

    def test_week_only_and_legacy_replies_do_not_add_a_five_hour_row(self):
        for duration in (10080, None):
            with self.subTest(duration=duration):
                result = quota_metrics({'rateLimits': {'primary': window(duration, 5), 'secondary': None}})
                self.assertEqual(result['remaining'], 95)
                self.assertEqual(result['short_remaining'], -1)

    def test_short_only_is_not_mislabeled_as_weekly(self):
        result = quota_metrics({'rateLimits': {'primary': window(300, 100)}})
        self.assertFalse(result['metrics_valid'])
        self.assertEqual(result['short_remaining'], 0)
        self.assertEqual(result['short_reset_at'], 0)


class SettingsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name)/'display-settings.json'
        self.store = Store(self.path)
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), handler(self.store))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def test_display_payload_clears_five_hour_row_when_window_disappears(self):
        week = window(10080, 4)
        self.store.update(**quota_metrics({'rateLimits': {'primary': week, 'secondary': window(300, 0, 1799500000)}}))
        status, data = self.request('GET', '/api/display')
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(data)['short_remaining'], 100)
        self.store.update(**quota_metrics({'rateLimits': {'primary': week}}))
        _, data = self.request('GET', '/api/display')
        payload = json.loads(data)
        self.assertEqual(payload['v'], 2)
        self.assertEqual(payload['remaining'], 96)
        self.assertEqual(payload['short_remaining'], -1)
        self.assertEqual(payload['short_reset_at'], 0)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.directory.cleanup()

    def request(self, method, path, value=None, token=True):
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        headers = {'Content-Type': 'application/json'}
        if token:
            headers['X-CSRF-Token'] = self.store.csrf
        connection.request(method, path, body=json.dumps(value).encode() if value is not None else None, headers=headers)
        response = connection.getresponse()
        result = response.status, response.read()
        connection.close()
        return result

    def test_custom_chinese_survives_restart_and_fits_protocol(self):
        self.assertEqual(self.request('POST', '/api/settings', {'header_text': '我的工作台'})[0], 200)
        restarted = Store(self.path).get()
        self.assertEqual(restarted['header_text'], '我的工作台')
        bitmap = base64.b64decode(restarted['header_bitmap'], validate=True)
        self.assertEqual(len(bitmap), 1600)
        self.assertGreater(len(set(bitmap)), 4) # Real antialiased glyphs, not blank/tofu data.
        self.assertLess(len(json.dumps(restarted, ensure_ascii=False).encode()), 4096)

    def test_invalid_names_do_not_replace_saved_name(self):
        self.store.configure('工作台')
        for value in ('', '   ', 'a\nb', 'a\x00b', '龙'*16, None, 42, {}):
            self.assertEqual(self.request('POST', '/api/settings', {'header_text': value})[0], 400, value)
            self.assertEqual(Store(self.path).get()['header_text'], '工作台')

    def test_cross_site_write_rejected(self):
        self.assertEqual(self.request('POST', '/api/settings', {'header_text': '改名'}, token=False)[0], 403)
        self.assertEqual(self.store.get()['header_text'], 'CODEX')
        self.assertFalse(self.path.exists())

    def test_settings_escape_custom_markup_and_expose_live_payload(self):
        self.store.configure('<b>')
        status, page = self.request('GET', '/settings')
        self.assertEqual(status, 200)
        self.assertIn(b'&lt;b&gt;', page)
        self.assertNotIn(b'value="<b>"', page)
        status, payload = self.request('GET', '/api/display')
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(payload)['header_text'], '<b>')
        clock = json.loads(payload)
        self.assertGreater(clock['now'], 1700000000)
        self.assertIsInstance(clock['utc_offset'], int)
        self.assertLessEqual(abs(clock['utc_offset']), 14*3600)
        self.store.configure('__TOKEN__')
        status, page = self.request('GET', '/settings')
        self.assertEqual(status, 200)
        self.assertIn(b'value="__TOKEN__"', page)

if __name__=='__main__':unittest.main()
