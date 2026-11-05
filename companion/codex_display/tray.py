"""Windowless Windows tray supervisor for the LAN display companion."""
import ctypes
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time
from urllib.request import urlopen
import webbrowser

from PIL import Image, ImageDraw
import pystray

ROOT = Path(__file__).resolve().parents[2]
HIDDEN = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
COLORS = {'running': '#51a9ff', 'waiting': '#ffce61', 'done': '#62d7a0',
          'idle': '#f5f8f6', 'offline': '#7f9c8b', 'error': '#ff6b6b'}


def address():
    # Prefer the Wi-Fi used by this display over simultaneous Ethernet/VPN routes.
    result = subprocess.run([
        'powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
        "Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias WLAN -ErrorAction SilentlyContinue | "
        "Where-Object {$_.IPAddress -notlike '169.254.*'} | Select-Object -First 1 -ExpandProperty IPAddress"
    ], capture_output=True, timeout=15, creationflags=HIDDEN)
    wifi = result.stdout.decode(errors='replace').strip()
    if wifi:
        socket.inet_aton(wifi)
        return wifi
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.connect(('192.0.2.1', 9))
        return connection.getsockname()[0]


def codex_binary():
    configured = os.environ.get('CODEX_APP_RUNTIME_CLI_PATH') or os.environ.get('CODEX_BIN')
    if configured and Path(configured).is_file():
        return configured
    result = subprocess.run([
        'powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
        '(Get-AppxPackage -Name OpenAI.Codex | Select-Object -First 1 -ExpandProperty InstallLocation)'
    ], capture_output=True, timeout=20, creationflags=HIDDEN, check=True)
    executable = Path(result.stdout.decode(errors='replace').strip()) / 'app/resources/codex.exe'
    if not executable.is_file():
        raise FileNotFoundError('Codex executable not found')
    return str(executable)


def icon_image(state):
    image = Image.new('RGBA', (64, 64))
    draw = ImageDraw.Draw(image)
    color = COLORS.get(state, COLORS['offline'])
    draw.rounded_rectangle((4, 4, 60, 60), radius=15, fill='#080d0b', outline=color, width=5)
    draw.line((26, 22, 17, 32, 26, 42), fill=color, width=5)
    draw.line((38, 22, 47, 32, 38, 42), fill=color, width=5)
    return image


class Companion:
    def __init__(self):
        self.process = None
        self.restart = threading.Event()
        self.stopped = threading.Event()
        self.device_ip = None
        self.caption = '正在连接'
        self.icon = pystray.Icon('CodexStatusDisplay', icon_image('offline'), 'Codex 状态屏 · 正在连接', menu=pystray.Menu(
            pystray.MenuItem(lambda item: self.caption, None, enabled=False),
            pystray.MenuItem('打开屏幕设置', self.open_settings, default=True),
            pystray.MenuItem('打开设备网页', self.open_device, enabled=lambda item: bool(self.device_ip)),
            pystray.MenuItem('重新连接', lambda icon, item: self.restart.set()),
            pystray.MenuItem('查看日志', lambda icon, item: os.startfile(ROOT / 'lan-tray.log')),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem('退出状态屏服务', self.quit)))

    def open_settings(self, icon=None, item=None):
        webbrowser.open('http://127.0.0.1:8738/settings')

    def open_device(self, icon=None, item=None):
        if self.device_ip:
            webbrowser.open(f'http://{self.device_ip}/')

    def quit(self, icon=None, item=None):
        subprocess.run(['schtasks.exe', '/Change', '/TN', 'Codex LAN Display', '/Disable'],
                       capture_output=True, timeout=15, creationflags=HIDDEN, check=True)
        self.stopped.set()
        self.icon.stop()

    def stop_worker(self):
        if self.process is not None:
            if self.process.poll() is None:
                # Terminate only this worker's tree, including its private app-server.
                subprocess.run(['taskkill.exe', '/PID', str(self.process.pid), '/T', '/F'],
                               capture_output=True, timeout=15, creationflags=HIDDEN)
                self.process.wait(timeout=10)
            self.process = None

    def supervise(self, icon):
        icon.visible = True
        logging.info('Tray ready; visible=%s', icon.visible)
        previous = None
        worker_address = None
        failures = 0
        try:
            while not self.stopped.is_set():
                try:
                    current_address = address()
                    if self.restart.is_set() or (worker_address and current_address != worker_address):
                        self.restart.clear()
                        self.stop_worker()
                    if self.process is None or self.process.poll() is not None:
                        with (ROOT / 'lan-worker.log').open('ab') as output:
                            self.process = subprocess.Popen([
                                str(Path(sys.executable).with_name('python.exe')), '-m',
                                'companion.codex_display.lan', '--codex-bin', codex_binary(), '--address', current_address
                            ], cwd=ROOT, stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                               creationflags=HIDDEN, env=dict(os.environ, PYTHONUTF8='1'))
                        worker_address = current_address
                        failures = 0
                        logging.info('Started LAN worker pid=%d address=%s', self.process.pid, worker_address)
                    with urlopen('http://127.0.0.1:8738/health', timeout=3) as response:
                        health = json.load(response)
                    failures = 0
                    device = health.get('device') or {}
                    self.device_ip = device.get('ip')
                    connected = time.time() - device.get('seen_at', 0) < 12
                    state = (health.get('state', 'unknown') if health.get('tasks_valid') else 'unknown') if connected else 'offline'
                    names = {'running': '任务进行中', 'waiting': '需要你回答', 'done': '任务已完成',
                             'idle': '待机', 'error': '任务异常', 'offline': '屏幕或 Codex 正在重连'}
                    self.caption = names.get(state, '状态待同步')
                except Exception as error:
                    state = 'offline'
                    self.caption = '连接恢复中'
                    failures += 1
                    if failures == 1:
                        logging.warning('Connection retry: %s', error)
                    if failures >= 3:
                        self.stop_worker()
                        failures = 0
                signature = (state, self.caption, self.device_ip)
                if signature != previous:
                    icon.icon = icon_image(state)
                    icon.title = 'Codex 状态屏 · ' + self.caption
                    icon.update_menu()
                    logging.info('%s; device=%s', self.caption, self.device_ip)
                    previous = signature
                self.stopped.wait(5)
        finally:
            self.stop_worker()
            logging.info('Tray supervisor stopped')

    def run(self):
        try:
            self.icon.run(setup=self.supervise)
            if not self.stopped.is_set():
                raise RuntimeError('Tray message loop exited unexpectedly')
        finally:
            self.stopped.set()


def main():
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s',
                        handlers=[RotatingFileHandler(ROOT / 'lan-tray.log', maxBytes=1024*1024,
                                                      backupCount=2, encoding='utf8')])
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    mutex = kernel.CreateMutexW(None, False, 'Local\\CodexStatusDisplayTray')
    if not mutex:
        raise ctypes.WinError(ctypes.get_last_error())
    if ctypes.get_last_error() == 183:
        kernel.CloseHandle(ctypes.c_void_p(mutex))
        return
    try:
        # A killed tray can leave its detached worker alive; replace only this project's worker.
        script = "$root = '" + str(ROOT).replace("'", "''") + "'; " + (
            "$workers = @(Get-CimInstance Win32_Process | Where-Object {"
            "$_.ExecutablePath -eq ($root + '\\companion\\.venv\\Scripts\\python.exe') -and "
            "$_.CommandLine -match '-m companion\\.codex_display\\.lan(?:\\s|$)'}); "
            "foreach ($worker in $workers) {taskkill.exe /PID $worker.ProcessId /T /F | Out-Null}")
        subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script],
                       capture_output=True, timeout=20, creationflags=HIDDEN, check=True)
        sys.excepthook = lambda kind, value, traceback: logging.critical(
            'Unhandled tray exception', exc_info=(kind, value, traceback))
        threading.excepthook = lambda args: logging.critical(
            'Unhandled thread exception: %s', args.thread.name,
            exc_info=(args.exc_type, args.exc_value, args.exc_traceback))
        Companion().run()
    finally:
        kernel.CloseHandle(ctypes.c_void_p(mutex))


if __name__ == '__main__':
    main()
