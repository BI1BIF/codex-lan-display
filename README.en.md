# Codex LAN Display

[简体中文](README.md) · English

A Wi-Fi desktop display for Codex usage and task activity. This edition targets Windows and the Waveshare ESP32-S3-Touch-AMOLED-2.16 board (480 × 480, 16 MB flash).

The display shows running, waiting for input, and completed states with distinct animated borders and optional sounds. After 15 seconds of inactivity it fades into a seven-segment clock. Usage remaining, available resets, recovery time, and battery status remain visible. Hold the touch screen to change brightness, volume, or Wi-Fi. The Windows tray companion starts at login and recovers from unexpected exits.

1. Install Arduino CLI and Python 3.12. Install the pinned ESP32 core and Arduino libraries using the commands in [the build guide](README.md#1-编译和刷入). Run build.ps1 and then flash.ps1 with the correct serial port. The flash script uses the board-specific partition layout; verify your board and back up any firmware you want to keep. Charging settings are for the original battery.
2. In the Codex desktop app, sign in to your account. Create a Python environment at companion/.venv and install companion/requirements-lan.txt. Run install-tray.ps1. The companion launches the local Codex app-server. For non-Store installations, set CODEX_BIN to the installed codex.exe.
3. Join the setup hotspot shown on the screen, open http://192.168.4.1/, and enter a 2.4 GHz Wi-Fi network that can reach the PC. Leave the PC address blank for mDNS, or enter its LAN IPv4 address. If necessary, allow TCP 8738 on the Windows private network.

The settings page is http://127.0.0.1:8738/settings. The device receives status and usage summaries rather than prompt text or account credentials. LAN HTTP is unauthenticated and unencrypted, so use a trusted LAN and do not expose the port to the Internet.

See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md). This edition is based on Jay Yao's Codex Usage Display.
