# Third-party notices

- Based on [jayyao97/codex-usage-display](https://github.com/jayyao97/codex-usage-display), revision 8bd4ec7adc39ab985bf743da6464152ca70698bb. Copyright 2026 Jay Yao, MIT. The original license is retained.
- The ES8311 driver comes from the Waveshare ESP32-S3-Touch-AMOLED-2.16 Arduino example. Copyright 2015–2022 Espressif Systems (Shanghai) CO LTD, Apache-2.0. Original notices are retained; see [Apache-2.0](LICENSES/Apache-2.0.txt).
- Chinese bitmap font subsets come from Noto Sans SC, SIL OFL 1.1. See [Noto copyright and license](src/fonts/OFL-NotoSansSC.txt).
- Clock font subsets come from [DSEG7 Classic Bold v0.46](https://github.com/keshikan/DSEG/releases/tag/v0.46) by keshikan. See [DSEG license](design/fonts/DSEG-LICENSE.txt).
- The E2 cues are original synthesized sounds. Source and regeneration scripts are in [design/sounds](design/sounds).

The Arduino libraries and Python packages are installed separately and retain their respective licenses.

## Binary releases

The Releases page provides the source for this application and the license and source bundle for the binary builds. The project code can be rebuilt with modified versions of bundled libraries using the build scripts; nothing in the project license prohibits debugging such modifications.

- Windows x64: CPython 3.12.13, Pillow 13.0.0, pystray 0.19.5, zeroconf 0.150.0, ifaddr 0.2.0 and six 1.17.0; packaged with [PyInstaller 6.19.0](https://github.com/pyinstaller/pyinstaller/tree/v6.19.0), whose bootloader has a distribution exception. Sources for the LGPL libraries pystray and zeroconf accompany the release. To use modified copies, install them into the companion virtual environment before running the packaging script.
- Firmware: [Arduino ESP32 3.3.8](https://github.com/espressif/arduino-esp32/tree/3.3.8), including its ESP32-S3 SDK package based on [ESP-IDF 5.5.4](https://github.com/espressif/esp-idf/tree/v5.5.4), and the Arduino libraries pinned in the README. Arduino ESP32 source accompanies the release. Install modified Arduino libraries or core into the Arduino CLI environment before running the build script. The SDK package records its component revisions in versions.txt.

The merged firmware is made with esptool 5.2.0 from the four files in the firmware build output:

    esptool --chip esp32s3 merge-bin --output Codex-LAN-Display-Waveshare-AMOLED-2.16.bin --flash-size keep 0x0 codex-lan-sketch.ino.bootloader.bin 0x8000 codex-lan-sketch.ino.partitions.bin 0x10000 codex-lan-sketch.ino.bin 0x810000 boot_app0.bin
