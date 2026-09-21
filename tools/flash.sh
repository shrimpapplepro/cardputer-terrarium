#!/bin/sh
# App-only flash over USB-Serial/JTAG (no download-mode button: esptool resets
# the chip itself via RTS). Board must already carry this project's partition
# table (first-time / clean flash: `pio run -t upload`, or the web installer).
#   TERRARIUM_PORT=/dev/ttyACM0 tools/flash.sh     # override the auto-detected port
set -e
cd "$(dirname "$0")/.."
PIO_HOME="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
PORT="${TERRARIUM_PORT:-$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no serial port found; plug the Cardputer in or set TERRARIUM_PORT" >&2; exit 1; }
pio run 2>&1 | grep -E "error|Error|SUCCESS|FAILED|RAM:|Flash:"
"$PIO_HOME/penv/bin/python" "$PIO_HOME/packages/tool-esptoolpy/esptool.py" --chip esp32s3 --no-stub \
  -p "$PORT" --before default_reset --after hard_reset \
  write_flash --flash_mode keep --flash_size keep 0x10000 .pio/build/cardputer_adv/firmware.bin 2>&1 | grep -E "Wrote|verified|rror"
