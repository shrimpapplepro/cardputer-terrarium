#!/bin/sh
# App-only flash over USB-Serial/JTAG (no download-mode button: esptool resets
# the chip itself via RTS). Board must already carry this project's partition
# table (first-time / clean flash: `pio run -t upload`).
set -e
cd "$(dirname "$0")/.."
~/.local/bin/pio run 2>&1 | grep -E "error|Error|SUCCESS|FAILED|RAM:|Flash:"
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --no-stub \
  -p /dev/cu.usbmodem101 --before default_reset --after hard_reset \
  write_flash --flash_mode keep --flash_size keep 0x10000 .pio/build/cardputer_adv/firmware.bin 2>&1 | grep -E "Wrote|verified|rror"
