"""Focused Windows supervisor checks; no tray UI or real child processes."""
import json
import unittest
from unittest.mock import Mock, patch

from companion.codex_display import tray


class TrayTests(unittest.TestCase):
    @patch.object(tray.pystray, 'Icon')
    def test_worker_exit_is_restarted_and_device_status_updates(self, icon):
        companion = tray.Companion()
        dead, alive = Mock(), Mock()
        dead.poll.return_value = 1
        alive.poll.return_value = None
        companion.process = dead
        payload = {'state': 'waiting', 'tasks_valid': True,
                   'device': {'ip': '192.0.2.20', 'seen_at': tray.time.time()}}
        response = Mock()
        response.read.return_value = json.dumps(payload).encode()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        companion.stopped.wait = lambda seconds: companion.stopped.set()
        with patch.object(tray, 'address', return_value='192.0.2.10'), \
             patch.object(tray, 'codex_binary', return_value='codex.exe'), \
             patch.object(tray.subprocess, 'Popen', return_value=alive) as spawn, \
             patch.object(tray, 'urlopen', return_value=response), \
             patch.object(companion, 'stop_worker') as stop:
            companion.supervise(companion.icon)
        self.assertEqual(spawn.call_count, 1)
        self.assertEqual(spawn.call_args.kwargs['creationflags'], tray.HIDDEN)
        self.assertEqual(companion.caption, '需要你回答')
        self.assertEqual(companion.device_ip, '192.0.2.20')
        stop.assert_called_once()

    def test_all_state_icons_are_visible_rgba(self):
        for state in tray.COLORS:
            icon = tray.icon_image(state)
            self.assertEqual(icon.size, (64, 64))
            self.assertIsNotNone(icon.getbbox())

    @patch.object(tray.pystray, 'Icon')
    def test_unexpected_tray_exit_fails_for_scheduler_recovery(self, icon):
        companion = tray.Companion()
        with self.assertRaisesRegex(RuntimeError, 'unexpectedly'):
            companion.run()
        self.assertTrue(companion.stopped.is_set())

    @patch.object(tray.pystray, 'Icon')
    @patch.object(tray.subprocess, 'run')
    def test_explicit_quit_disables_automatic_relaunch(self, run, icon):
        companion = tray.Companion()
        companion.quit()
        self.assertIn('/Disable', run.call_args.args[0])
        self.assertTrue(companion.stopped.is_set())
        companion.run()


if __name__ == '__main__':
    unittest.main()
