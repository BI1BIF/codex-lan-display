"""Read-only LAN companion. No prompts, account tokens or task text leave the PC."""
import argparse
import asyncio
import json
import html
import secrets
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import socket
import threading
import time
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from .app_server import AppServerClient
from .display_settings import DisplaySettings


def records_state(records, checkpoint=None):
    state, pending = checkpoint if checkpoint is not None else ('unknown', {})
    for record in records:
        p = record.get('payload') or {}
        if not isinstance(p, dict):
            continue
        kind = p.get('type')
        if record.get('type') == 'event_msg':
            if kind == 'task_started':
                state, pending = 'running', {}
            elif kind in ('task_complete', 'turn_aborted'):
                state = 'error' if p.get('error') else ('idle' if kind == 'turn_aborted' else 'done')
                pending = {}
            elif kind == 'user_message':
                pending = {k:v for k,v in pending.items() if v == 'approval'}
            elif kind in ('request_user_input', 'approval_request'):
                pending[p.get('id', kind)] = 'question' if kind == 'request_user_input' else 'approval'
            elif kind in ('server_request_resolved', 'request_resolved'):
                pending.pop(p.get('request_id'), None)
        if record.get('type') != 'response_item':
            continue
        if kind == 'message' and p.get('role') == 'user':
            pending = {k:v for k,v in pending.items() if v == 'approval'}
        if kind in ('function_call', 'custom_tool_call'):
            name = p.get('name', '').lower()
            args = p.get('arguments', '') or p.get('input', '')
            if 'request_user_input' in name:
                pending[p.get('call_id')] = 'async_question' if 'async' in name else 'question'
            elif 'requestapproval' in name or 'require_escalated' in str(args):
                pending[p.get('call_id')] = 'approval'
        elif kind in ('function_call_output', 'custom_tool_call_output'):
            key = p.get('call_id')
            if pending.get(key) != 'async_question':
                pending.pop(key, None)
    if checkpoint is not None:
        checkpoint[:] = [state, pending]
    return 'waiting' if pending else state


def rollout_start(f, size):
    """Find the latest lifecycle event without parsing the entire history."""
    pos, carry = size, b''
    while pos:
        end = pos
        pos = max(0, pos - 4*1024*1024)
        f.seek(pos)
        lines = (f.read(end-pos) + carry).split(b'\n')
        carry = lines.pop(0) if pos else b''
        offset = pos + (len(carry)+1 if pos else 0)
        candidate = None
        for line in lines:
            if b'"event_msg"' in line and any(k in line for k in
                    (b'"task_started"', b'"task_complete"', b'"turn_aborted"')):
                try:
                    row = json.loads(line)
                    if row.get('type') == 'event_msg' and (row.get('payload') or {}).get('type') in (
                            'task_started', 'task_complete', 'turn_aborted'):
                        candidate = offset
                except (ValueError, AttributeError):
                    pass
            offset += len(line)+1
        if candidate is not None:
            return candidate
    return 0


class Rollouts:
    def __init__(self):
        self.cache = {}

    def state(self, path):
        try:
            with open(path, 'rb') as f:
                stat = os.fstat(f.fileno())
                key = (stat.st_size, stat.st_mtime_ns)
                old = self.cache.get(path)
                if old and old[0] == key:
                    state = old[4]
                else:
                    if old and old[1] == stat.st_ino and stat.st_size > old[0][0]:
                        offset, checkpoint = old[2], old[3]
                    else:
                        offset = rollout_start(f, stat.st_size)
                        checkpoint = ['unknown', {}]
                    f.seek(offset)

                    def records():
                        nonlocal offset
                        for raw in f:
                            if not raw.endswith(b'\n'):
                                break
                            offset += len(raw)
                            try:
                                yield json.loads(raw)
                            except ValueError:
                                pass

                    state = records_state(records(), checkpoint)
                    self.cache[path] = (key, stat.st_ino, offset, checkpoint, state)
            # Preserve the existing stale-session guard.
            if state in ('running', 'waiting') and time.time()-stat.st_mtime > 1800:
                return 'unknown'
            return state
        except OSError:
            return 'unknown'


def aggregate(states):
    running = states.count('running')
    waiting = states.count('waiting')
    if waiting:
        state = 'waiting'
    elif running:
        state = 'running'
    else:
        state = next((s for s in states if s in ('done', 'error', 'idle')),
                     'unknown' if states else 'idle')
    return state, running, waiting


def recent_rollouts(root, now):
    # shortcut: scan today and yesterday for sessions missing from the index.
    # Older sessions are already tracked through thread/list.
    for day in (now, now - timedelta(days=1)):
        yield from (root / day.strftime('%Y/%m/%d')).glob('rollout-*.jsonl')


def task_states(threads, rollouts, local_paths, now):
    by_id = {thread['id']: thread for thread in threads if thread.get('id')}
    for path in local_paths:
        try:
            mtime = path.stat().st_mtime
            if now - mtime > 1800:
                continue
            with path.open('rb') as source:
                header = json.loads(source.readline(65536))
            meta = header.get('payload') or {}
            if header.get('type') != 'session_meta' or not isinstance(meta, dict):
                continue
            thread_id = meta.get('id')
            origin = meta.get('source')
            if not isinstance(thread_id, str) or not thread_id or (
                isinstance(origin, dict) and 'subagent' in origin
            ):
                continue
            indexed = by_id.get(thread_id, {})
            indexed_path = indexed.get('path')
            if indexed_path:
                try:
                    if os.path.getmtime(indexed_path) >= mtime:
                        continue
                except OSError:
                    pass
            by_id[thread_id] = dict(indexed, path=str(path))
        except (OSError, ValueError, AttributeError):
            continue

    for thread in by_id.values():
        status = thread.get('status') or {}
        flags = status.get('activeFlags') or []
        if any(flag in flags for flag in ('waitingOnUserInput', 'waitingOnApproval')):
            yield 'waiting'
        elif status.get('type') == 'active':
            yield 'running'
        else:
            state = rollouts.state(thread.get('path') or '')
            yield 'idle' if state == 'unknown' and status.get('type') == 'idle' else state


def quota_metrics(result):
    limits = (result.get('rateLimitsByLimitId') or {}).get('codex') or result.get('rateLimits') or {}
    primary = limits.get('primary') or {}
    windows = {w.get('windowDurationMins'): w for w in (primary, limits.get('secondary') or {}) if w}
    # Older servers omit the duration. Keep the previous single-window behavior for those replies.
    weekly = windows.get(10080) or (primary if primary.get('windowDurationMins') is None else {})
    short = windows.get(300, {})

    def remaining(window):
        used = window.get('usedPercent')
        return 100 - int(max(0, min(100, used))) if type(used) in (int, float) else -1

    week_remaining = remaining(weekly)
    credits = result.get('rateLimitResetCredits') or {}
    return dict(remaining=max(0, week_remaining), metrics_valid=week_remaining >= 0,
                reset_at=int(weekly.get('resetsAt') or 0),
                short_remaining=remaining(short), short_reset_at=int(short.get('resetsAt') or 0),
                credits=max(0, int(credits.get('availableCount', 0))),
                credits_valid='availableCount' in credits, metrics_at=int(time.time()))


class Store:
    def __init__(self, settings_path=None):
        self.lock = threading.Lock()
        self.settings = DisplaySettings(settings_path)
        self.csrf = secrets.token_urlsafe(32)
        self.value = {'v': 2, 'state': 'unknown', 'running': 0, 'waiting': 0,
                      'metrics_valid': False, 'tasks_valid': False,
                      'remaining': 0, 'credits': 0, 'reset_at': 0}
        self.last_device = 0
        self.device_ip = None

    def update(self, **values):
        with self.lock:
            self.value.update(values)

    def get(self):
        with self.lock:
            return dict(self.value, **self.settings.payload(), now=int(time.time()),
                        utc_offset=int(datetime.now().astimezone().utcoffset().total_seconds()))

    def configure(self, text):
        with self.lock:
            self.settings.save(text)
            return {'header_text': self.settings.text}


def handler(store):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.0'
        def reply(self, code, data, content_type='application/json; charset=utf-8'):
            if not isinstance(data, bytes):
                data = json.dumps(data, ensure_ascii=False, separators=(',', ':')).encode('utf8')
            self.send_response(code)
            self.send_header('Content-Type', content_type)
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('X-Frame-Options', 'DENY')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = self.path.split('?')[0]
            if path in ('/', '/settings'):
                page = Path(__file__).with_name('settings.html').read_text(encoding='utf8')
                page = page.replace('__TOKEN__', store.csrf).replace('__LABEL__', html.escape(store.get()['header_text'], quote=True))
                self.reply(200, page.encode('utf8'), 'text/html; charset=utf-8')
                return
            if path not in ('/health', '/api/display'):
                self.send_error(404)
                return
            if path == '/api/display':
                store.last_device = time.time()
                store.device_ip = self.client_address[0]
            value = store.get()
            if path == '/health':
                value['device'] = {'ip': store.device_ip, 'seen_at': int(store.last_device)}
            self.reply(200, value)

        def do_POST(self):
            if self.path != '/api/settings':
                self.send_error(404)
                return
            if not secrets.compare_digest(self.headers.get('X-CSRF-Token', ''), store.csrf):
                self.reply(403, {'error': '页面已失效，请刷新后重试。'})
                return
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 512:
                    raise ValueError('请求长度无效。')
                value = json.loads(self.rfile.read(length))
                if not isinstance(value, dict):
                    raise ValueError('设置格式无效。')
                result = store.configure(value.get('header_text'))
            except (ValueError, UnicodeDecodeError) as error:
                self.reply(400, {'error': str(error)})
                return
            except OSError:
                logging.exception('Screen preferences could not be saved')
                self.reply(500, {'error': '电脑无法保存设置，请检查磁盘空间。'})
                return
            self.reply(200, result)
        def log_message(self, *args):
            pass
    return Handler


async def poll(store, codex):
    rollouts = Rollouts()
    session_root = Path(os.environ.get('CODEX_HOME') or Path.home() / '.codex') / 'sessions'
    while True:
        c = AppServerClient(codex)
        try:
            await c.start()
            next_metrics = 0
            while True:
                if time.monotonic() >= next_metrics:
                    try:
                        result = await c.request('account/rateLimits/read')
                        store.update(**quota_metrics(result))
                        next_metrics = time.monotonic()+45
                    except Exception:
                        store.update(metrics_valid=False, short_remaining=-1)
                        next_metrics = time.monotonic()+10
                        logging.exception('Quota refresh failed')
                threads = []
                cursor = None
                for _ in range(5):
                    params = {'limit': 100, 'sortKey': 'recency_at', 'sortDirection': 'desc', 'useStateDbOnly': True}
                    if cursor:
                        params['cursor'] = cursor
                    result = await c.request('thread/list', params)
                    threads.extend(result.get('data') or [])
                    cursor = result.get('nextCursor')
                    if not cursor:
                        break
                now = datetime.now()
                states = list(task_states(threads, rollouts, recent_rollouts(session_root, now), now.timestamp()))
                state, running, waiting = aggregate(states)
                previous = store.get()
                store.update(state=state, running=running, waiting=waiting,
                             tasks_valid=state != 'unknown', tasks_at=int(time.time()))
                if (previous['state'],previous['running'],previous['waiting']) != (state,running,waiting):
                    logging.info('Status: %s, running=%d, waiting=%d',state,running,waiting)
                await asyncio.sleep(2)
        except Exception:
            store.update(tasks_valid=False)
            logging.exception('Codex connection failed; reconnecting')
        finally:
            await c.stop()
        await asyncio.sleep(5)


def main(data_dir=None):
    data_dir = data_dir or Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser()
    p.add_argument('--codex-bin', required=True)
    p.add_argument('--address', required=True)
    p.add_argument('--port', type=int, default=8738)
    args = p.parse_args()
    log = data_dir / 'lan-companion.log'
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s',
                        handlers=[RotatingFileHandler(log,maxBytes=1024*1024,backupCount=2,encoding='utf-8')])
    from zeroconf import Zeroconf, ServiceInfo
    store = Store(data_dir / 'display-settings.json')
    server = ThreadingHTTPServer(('0.0.0.0',args.port),handler(store))
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever,daemon=True).start()
    z = Zeroconf()
    info = ServiceInfo('_codexdisplay._tcp.local.', 'Codex LAN Display._codexdisplay._tcp.local.',
                       addresses=[socket.inet_aton(args.address)],port=args.port,
                       properties={'v':'2'},server='codex-display-pc.local.')
    z.register_service(info)
    try:
        asyncio.run(poll(store,args.codex_bin))
    finally:
        z.unregister_service(info)
        z.close()
        server.shutdown()

if __name__ == '__main__':
    main()
