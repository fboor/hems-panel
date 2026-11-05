#!/usr/bin/env bash
set -euo pipefail
# Build and run the WARP panel emulator.
#
#   sim/run.sh                                  window, mock on :8082
#   sim/run.sh --url http://wem2-abcd/json      real device
#   sim/run.sh --shot out.png --after 3         one frame, then exit
#   sim/run.sh --probe                          raw JSON to stdout
#
# LVGL comes from the PlatformIO library directory. Set LVGL_DIR if it lives
# somewhere else; the default is the sibling rct-panel checkout, which is only
# read, never written.

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
build="$here/build"
lvgl=${LVGL_DIR:-$repo/../rct-panel/.pio/libdeps/esp32-s3/lvgl}
out="$build/warp_panel_sim"

if [ ! -d "$lvgl/src" ]; then
  echo "LVGL nicht gefunden: $lvgl/src" >&2
  echo "LVGL_DIR setzen oder PlatformIO einmal laufen lassen." >&2
  exit 1
fi

mkdir -p "$build/lvgl"

cflags="-O2 -DLV_CONF_INCLUDE_SIMPLE -I$here/lvconf -I$repo/include -I$repo/src -I$lvgl"

# LVGL once, keyed by source path.
neu=0
while IFS= read -r f; do
  rel=${f#"$lvgl/src/"}
  o="$build/lvgl/${rel//\//_}.o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
    cc $cflags -c "$f" -o "$o" &
    neu=$((neu + 1))
    if [ $((neu % 32)) -eq 0 ]; then wait; fi
  fi
done <<EOF
$(find "$lvgl/src" -name '*.c')
EOF
wait
# Compile chart_bg.c as C
cc $cflags -c "$repo/src/chart_bg.c" -o "$build/chart_bg.o" 2>/dev/null && echo "chart_bg compiled"

# The generated fonts are C (designated initializers), not C++.
mkdir -p "$build/fonts"
font_objs=()
for f in "$repo"/src/ui/fonts/*.c; do
  o="$build/fonts/$(basename "${f%.c}").o"
  cc $cflags -c "$f" -o "$o"
  font_objs+=("$o")
done

c++ $cflags -std=c++17 \
  "$here/sim_main.cpp" \
  "$repo/src/ui/WarpUi.cpp" \
  "$build"/lvgl/*.o \
  "$build"/chart_bg.o \
  "${font_objs[@]}" \
  -o "$out" -lSDL2 -lz -lm -lpthread

echo "gebaut: $out"
exec "$out" "$@"
