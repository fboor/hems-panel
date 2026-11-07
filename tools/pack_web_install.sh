#!/bin/sh
# Build the browser installer's binaries and generate its manifests.
#
# A blank panel has no bootloader and no partition table, so both have to arrive or
# nothing boots - three parts, not one. flash.sh writes firmware.bin alone and is
# right to: it updates a panel that is already running, where the running bootloader
# hands over to the new app. On an empty device there is no bootloader to hand over,
# which is why this stages more than one.
#
# The bootloader goes to 0x0, not 0x1000. The ESP Web Tools documentation writes
# 4096 and that example is for the ESP32; the S3 has its bootloader at the very
# start of the flash, which the espressif32 builder says in one line:
#   "0x0" if mcu in ("esp32c3", "esp32c6", "esp32s3") else "0x1000"
# Written to 0x1000 it landed where nothing reads it, and since an install erases
# the whole chip first the real bootloader at 0x0 was gone.
#
# No boot_app0.bin either: without a factory partition the ESP-IDF bootloader boots
# the app at 0x10000 directly, and the example's file would land in the partition
# the bootloader reads first to decide where to boot.
#
# ONE LIST, TWO READERS
#
# docs/install/devices.json says which devices exist and which build environment
# each language of each device comes from. The page reads it to fill its device
# picker, this script reads it to know what to build. Adding a device is an edit
# there and nowhere else.
#
# ONE VERSION
#
# include/FirmwareVersion.h holds the release number, and this reads it from there.
# The manifests are generated, so a committed one could never carry a stale number.
#
# WHY PYTHON AND NOT SHELL
#
# devices.json is JSON, and a shell that parsed JSON would be the fragile part of a
# script whose job is to produce flashable files. So python reads it once and hands
# this script plain lines; python writes the manifests for the same reason.
#
# SPDX-License-Identifier: MIT
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/docs/install"
HEADER="$ROOT/include/FirmwareVersion.h"
LIST="$OUT/devices.json"
PIO=${PIO:-pio}
ESPROOT=${ESPROOT:-$HOME/.platformio/packages}
for f in "$HEADER" "$LIST"; do
  [ -f "$f" ] || { echo "Missing: $f"; exit 1; }
done

eval "$(python3 - "$HEADER" "$LIST" <<'PY'
import json, os, re, shlex, sys

header, liste = sys.argv[1], sys.argv[2]
h = open(header).read()
def holen(name):
    m = re.search(r'^#define\s+%s\s+"([^"]*)"' % name, h, re.M)
    if not m:
        sys.exit("Version not found in %s: %s" % (header, name))
    return m.group(1)

version, monat = holen("WARP_FW_VERSION"), holen("WARP_FW_VERSION_MON")
release = "v" + version
print("VERSION=%s" % shlex.quote(version))
print("RELEASE=%s" % shlex.quote(release))
print("MONTH=%s" % shlex.quote(monat))
print("FULL=%s" % shlex.quote("%s (%s)" % (version, monat)))

d = json.load(open(liste))
if not d.get("devices"):
    sys.exit("No devices in %s" % liste)

aufgaben = []
for dev in d["devices"]:
    for sprache in dev["envs"]:
        env = dev["envs"].get(sprache)
        if not env:
            sys.exit("Device %s: no build environment for %s" % (dev["id"], sprache))
        aufgaben.append((dev["id"], sprache, env))

envs = []
gesehen = set()
for _, _, env in aufgaben:
    if env not in gesehen:
        gesehen.add(env)
        envs.append(env)
print("ENVS=%s" % shlex.quote(" ".join(envs)))
print("FIRST_ENV=%s" % shlex.quote(envs[0]))
print("ANZAHL_AUFGABEN=%d" % len(aufgaben))

zeilen = []
for dev_id, sprache, env in aufgaben:
    zeilen.append("%s\t%s\t%s" % (dev_id, sprache, env))
print("PLAN_LINES=%s" % shlex.quote("\n".join(zeilen)))

release_dir = "%s/%s" % (os.path.dirname(liste), release)
os.makedirs(release_dir, exist_ok=True)

manifests = []
for dev in d["devices"]:
    for sprache in dev["envs"]:
        ziel = "%s/manifest-%s-%s.json" % (release_dir, dev["id"], sprache)
        with open(ziel, "w") as fh:
            fh.write("""{
  "name": "%(name)s (%(lang)s)",
  "version": "%(version)s (%(month)s)",
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "%(chip)s",
      "improv": false,
      "parts": [
        { "path": "%(board)s-bootloader.bin", "offset": 0 },
        { "path": "%(board)s-partitions.bin", "offset": 32768 },
        { "path": "%(board)s-%(lang)s.bin", "offset": 65536 }
      ]
    }
  ]
}
""" % {"name": dev["name"], "lang": sprache, "version": version,
       "month": monat, "chip": dev["chipFamily"], "release": release,
       "board": dev["id"]})
        manifests.append("manifest-%s-%s.json" % (dev["id"], sprache))

with open(liste) as fh:
    inhalt = fh.read()
if re.search(r'^\s*"release"\s*:', inhalt, re.M):
    inhalt = re.sub(r'^(\s*)"release"\s*:\s*".*",?\s*$',
                    r'\1"release": "%s",' % release, inhalt, flags=re.M)
else:
    inhalt = inhalt.replace(
        '"devices": [',
        '"release": "%s",\n  "devices": [' % release, 1)
with open(liste, "w") as fh:
    fh.write(inhalt)

print("MANIFESTS=%s" % shlex.quote(" ".join(manifests)))
PY
)"

PLAN=$(printf '%s\n' "$PLAN_LINES")

echo "=== Release $FULL ($RELEASE) ==="
echo "Devices: $ANZAHL_AUFGABEN language variants, $(echo "$PLAN" | wc -l) entries"
echo
echo "=== 1/3 Build firmware ==="
for env in $ENVS; do
  echo "--- $env"
  "$PIO" run -e "$env"
done

echo
echo "=== 2/3 Stage parts ==="
for part in bootloader partitions; do
  for device in $(printf '%s\n' "$PLAN" | cut -f1 | sort -u); do
    erste=$(printf '%s\n' "$PLAN" | awk -F'\t' -v d="$device" '$1 == d { print $3; exit }')
    src="$ROOT/.pio/build/$erste/$part.bin"
    ziel="$OUT/$RELEASE/$device-$part.bin"
    if [ -f "$ziel" ] && ! cmp -s "$src" "$ziel"; then
      echo "  $device/$part differs from the staged $RELEASE file - the builds"
      echo "  use different flash layouts. Only one of them can be right."
      exit 1
    fi
    cp "$src" "$ziel"
    echo "  $RELEASE/$device-$part.bin  ($erste)"
  done
done

printf '%s\n' "$PLAN" | while IFS='	' read -r device sprache env; do
  cp "$ROOT/.pio/build/$env/firmware.bin" "$OUT/$RELEASE/$device-$sprache.bin"
  echo "  $RELEASE/$device-$sprache.bin  ($env)"
done

echo
echo "=== 3/3 Manifests written ==="
for m in $MANIFESTS; do
  echo "  $m"
done

python3 - "$OUT/$RELEASE" "$RELEASE" <<'PY3'
import json, os, sys
ordner, release = sys.argv[1], sys.argv[2]
fehlend = []
for name in sorted(os.listdir(ordner)):
    if not name.startswith("manifest-"):
        continue
    with open(os.path.join(ordner, name)) as fh:
        m = json.load(fh)
    for build in m["builds"]:
        for p in build["parts"]:
            pfad = os.path.join(ordner, p["path"])
            if not os.path.isfile(pfad):
                fehlend.append("%s -> %s" % (name, p["path"]))
if fehlend:
    sys.exit("  Parts missing that the manifests name:\n" +
             "\n".join("    " + f for f in fehlend) +
             "\n  Every path was checked against the manifest's own folder (%s),"
             "\n  because that is how ESP Web Tools resolves it - not against the page."
             % release)
print("  all part paths of all %d manifests resolved" %
      len([n for n in os.listdir(ordner) if n.startswith("manifest-")]))
PY3

echo
echo "--- Staged ---"
ls -l "$OUT/$RELEASE" | tail -n +2 | awk 'NF>5 {printf "  %-40s %8.1f kB\n", $9, $5/1024}'

cat <<EOF

Release $FULL, staged as docs/install/$RELEASE/. The number lives in
include/FirmwareVersion.h; the web interface, page and manifests read it from
there.

This folder is versioned, manifests and .bin files together: the page serves
from /docs exactly what is in there, and a manifest without its files installs
nothing. An older release folder stays and remains reachable.

All files of a release are byte-reproducible - building the same release folder
again yields the same bytes, or the comparison here aborts.

For GitHub Pages: Settings -> Pages -> Source: "Deploy from a branch", Branch
"master", Folder "/docs". Rebuilding means: run this and commit the folder.
EOF
