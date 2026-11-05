"""Entry point for the standalone Windows tray and its LAN worker."""
import sys

from companion.codex_display import lan, tray


if __name__ == '__main__':
    if sys.argv[1:2] == ['--worker']:
        del sys.argv[1]
        tray.ROOT.mkdir(parents=True, exist_ok=True)
        # A windowed executable has no Python stdout/stderr.
        with (tray.ROOT / 'lan-worker.log').open('a', encoding='utf8') as output:
            sys.stdout = sys.stderr = output
            lan.main(tray.ROOT)
    else:
        tray.main()
