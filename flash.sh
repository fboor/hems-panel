#!/usr/bin/env bash
set -euo pipefail
# Build and flash the panel firmware onto a Waveshare ESP32-S3-Touch-LCD-7.
#
#   ./flash.sh                    modified board (EXIO2/DISP trace cut)
#   ./flash.sh --factory          factory board (trace intact)
#   ./flash.sh --build-only       build only; no board needed
#   ./flash.sh --merged [file]    build one 0x0 image; no board needed
#   ./flash.sh --erase            erase the whole 16 MB flash first
#   ./flash.sh --monitor          open the serial monitor after flashing
#   ./flash.sh --port /dev/...    serial port instead of auto-detection
#
# The two board variants are different images, not a runtime setting:
# with the cut GPIO 6 owns the backlight and can dim it; without the cut the
# light hangs on EXIO2 and GPIO 6 must stay away from that net. Flashing the
# wrong one puts two push-pull outputs on the same enable line.
#
# Serial log: the application's Serial is the native USB CDC port. The UART
# Type-C port is for flashing and for the ROM output. See README.md.

here=$(cd "$(dirname "$0")" && pwd)
invoke_dir=$PWD
cd "$here"

env=esp32-s3-touch-lcd-7
variant="modifiziert (EXIO2/DISP getrennt)"
port=""
do_erase=0
do_monitor=0
build_only=0
merged_requested=0
merged_file=""

usage() {
  sed -n '3,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case "$1" in
    --factory)
      env=esp32-s3-touch-lcd-7-factory
      variant="factory (EXIO2/DISP verbunden)"
      shift
      ;;
    --modified)
      env=esp32-s3-touch-lcd-7
      variant="modifiziert (EXIO2/DISP getrennt)"
      shift
      ;;
    --port)
      if [ $# -lt 2 ] || [ -z "${2:-}" ]; then
        echo "--port braucht einen Geraetenamen, z.B. /dev/ttyACM0" >&2
        exit 2
      fi
      port=$2
      shift 2
      ;;
    --erase)
      do_erase=1
      shift
      ;;
    --monitor)
      do_monitor=1
      shift
      ;;
    --build-only)
      build_only=1
      shift
      ;;
    --merged)
      merged_requested=1
      shift
      if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then
        merged_file=$1
        shift
      fi
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unbekannte Option: $1" >&2
      echo >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ "$build_only" -eq 1 ] && [ "$merged_requested" -eq 1 ]; then
  echo "--build-only und --merged schliessen sich aus." >&2
  exit 2
fi

echo "Board:  $variant"
echo "Env:    $env"
echo

pio run -e "$env"

build_dir="$here/.pio/build/$env"
app="$build_dir/firmware.bin"
app_limit=$((0x700000)) # app0 in partitions/16mb_app.csv

for f in "$app" "$build_dir/bootloader.bin" "$build_dir/partitions.bin"; do
  if [ ! -f "$f" ]; then
    echo "Build unvollstaendig: $f fehlt." >&2
    exit 1
  fi
done

app_size=$(stat -c %s "$app")
if [ "$app_size" -gt "$app_limit" ]; then
  echo "Firmware ist $app_size Bytes gross, app0 fasst nur $app_limit." >&2
  exit 1
fi
echo
echo "App:    $app_size Bytes (Grenze $app_limit, $(awk \
  -v a="$app_size" -v l="$app_limit" 'BEGIN { printf "%.1f", 100 * a / l }') %)"
echo "Layout: bootloader 0x000000, Partitionen 0x008000, boot_app0 0x00e000, App 0x010000"

if [ "$merged_requested" -eq 1 ]; then
  boot_app0=""
  while IFS= read -r candidate; do
    boot_app0=$candidate
    break
  done < <(find "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages" \
    -path '*/framework-arduinoespressif32*/tools/partitions/boot_app0.bin' \
    -print 2>/dev/null | sort)

  if [ -z "$boot_app0" ]; then
    echo "boot_app0.bin nicht gefunden. Einmal 'pio run' ausfuehren." >&2
    exit 1
  fi

  if [ -z "$merged_file" ]; then
    merged_file="$build_dir/firmware-merged.bin"
  elif [ "${merged_file#/}" = "$merged_file" ]; then
    merged_file="$invoke_dir/$merged_file"
  fi
  mkdir -p "$(dirname "$merged_file")"

  echo "Merged: $merged_file"
  pio pkg exec -p tool-esptoolpy@1.40501.0 -- esptool.py \
    --chip esp32s3 merge_bin \
    -o "$merged_file" \
    --flash_mode keep --flash_freq 80m --flash_size 16MB \
    0x0 "$build_dir/bootloader.bin" \
    0x8000 "$build_dir/partitions.bin" \
    0xe000 "$boot_app0" \
    0x10000 "$app"

  merged_size=$(stat -c %s "$merged_file")
  echo
  echo "Fertig: $merged_file ($merged_size Bytes)"
  echo "Flashen:"
  echo "  esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \\"
  echo "    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \\"
  echo "    0x0 '$merged_file'"
  exit 0
fi

if [ "$build_only" -eq 1 ]; then
  echo
  if [ "$env" = "esp32-s3-touch-lcd-7-factory" ]; then
    echo "Nur gebaut. Flashen mit: ./flash.sh --factory --port /dev/..."
  else
    echo "Nur gebaut. Flashen mit: ./flash.sh --port /dev/..."
  fi
  exit 0
fi

if [ -z "$port" ]; then
  port=$(pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
try:
    devices = json.load(sys.stdin)
except Exception:
    devices = []
ports = [d.get("port") for d in devices if d.get("port")]
wch = [d["port"] for d in devices
       if d.get("port") and "1A86" in str(d.get("hwid", "")).upper()]
esp = [d["port"] for d in devices
       if d.get("port") and "303A" in str(d.get("hwid", "")).upper()]
if len(wch) == 1:
    print(wch[0])
elif len(ports) == 1:
    print(ports[0])
elif len(esp) == 1 and not wch:
    print(esp[0])
elif ports:
    sys.stderr.write("Mehrere serielle Geraete gefunden:\n")
    for d in devices:
        if d.get("port"):
            sys.stderr.write("  %s  %s  %s\n" % (
                d["port"], d.get("description", ""), d.get("hwid", "")))
    sys.stderr.write("Bitte mit --port das UART-Geraet waehlen.\n")
    sys.exit(1)
' 2>&1) || {
    echo "$port" >&2
    echo >&2
    echo "Kein Board gefunden. USB-Kabel an den UART-Type-C-Port stecken," >&2
    echo "BOOT halten, RESET druecken, BOOT loslassen und erneut versuchen." >&2
    echo "Nur bauen: ./flash.sh --build-only" >&2
    exit 1
  }
fi

if [ -z "$port" ]; then
  echo "Kein Board gefunden. USB-Kabel an den UART-Type-C-Port stecken," >&2
  echo "BOOT halten, RESET druecken, BOOT loslassen und erneut versuchen." >&2
  echo "Nur bauen: ./flash.sh --build-only" >&2
  exit 1
fi

if [ "${port#/}" != "$port" ] && [ ! -e "$port" ]; then
  echo "Serieller Port $port existiert nicht." >&2
  exit 1
fi
if [ "${port#/}" != "$port" ] && { [ ! -r "$port" ] || [ ! -w "$port" ]; }; then
  echo "Kein Zugriff auf $port. Mitglied der Gruppe dialout sein:" >&2
  echo "  sudo usermod -aG dialout $USER   (danach neu anmelden)" >&2
  exit 1
fi

echo
echo "Port:   $port"
if [ "$do_erase" -eq 1 ]; then
  echo
  echo "Loesche den gesamten Flash (NVS und WLAN-Zugang gehen verloren) ..."
  pio run -t erase -e "$env" --upload-port "$port"
fi

echo
echo "Flashe ..."
pio run -t upload -e "$env" --upload-port "$port"

echo
echo "Fertig. RESET am Board druecken, falls der Upload es nicht selbst tut."

if [ "$do_monitor" -eq 1 ]; then
  monitor_port=$port

  # The UART port carries the ROM output; the application's Serial is the native
  # USB CDC port, which appears once the firmware has switched EXIO5 to USB.
  if pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
try:
    devices = json.load(sys.stdin)
except Exception:
    devices = []
port = sys.argv[1]
sys.exit(0 if any(
    d.get("port") == port and "1A86" in str(d.get("hwid", "")).upper()
    for d in devices) else 1)
' "$port"; then
    echo "Warte auf den nativen USB-Port fuer das Anwendungslog ..."
    for _ in $(seq 1 20); do
      native=$(pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
try:
    devices = json.load(sys.stdin)
except Exception:
    devices = []
for d in devices:
    if d.get("port") and "303A" in str(d.get("hwid", "")).upper():
        print(d["port"])
        break
' || true)
      if [ -n "$native" ] && [ "$native" != "$port" ]; then
        monitor_port=$native
        break
      fi
      sleep 0.5
    done
  fi

  if [ "$monitor_port" = "$port" ]; then
    echo "Monitor auf $port. Der UART-Port zeigt die ROM-Ausgabe; das"
    echo "Anwendungslog liegt am nativen USB-Port (303A:1001)."
  else
    echo "Monitor auf $monitor_port (nativer USB-Port)."
  fi
  pio device monitor -e "$env" --port "$monitor_port" -b 115200
else
  echo "Monitor: pio device monitor -e $env --port $port"
  echo "Anwendungslog: nativer USB-Port (303A:1001), nicht der UART-Port."
fi
