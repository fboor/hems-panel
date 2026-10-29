#!/usr/bin/env bash
set -euo pipefail
# Start the mock and the panel emulator together.
#
#   ./run.sh                         mock on :8082 + emulator
#   ./run.sh --shot out.png          one frame, then exit
#   ./run.sh --probe                 print the JSON the emulator would read
#   ./run.sh --url http://wem/json   real device, no mock
#
# Any argument is passed through to sim/run.sh.

here=$(cd "$(dirname "$0")" && pwd)
mock_pid=""

url_given=0
for a in "$@"; do
  if [ "$a" = "--url" ]; then
    url_given=1
  fi
done

if [ "$url_given" -eq 0 ]; then
  if ! (exec 3<>/dev/tcp/127.0.0.1/8082) 2>/dev/null; then
    python3 "$here/tools/warp_mock/warp_mock.py" \
      --port 8082 --scenario midsummer --live &
    mock_pid=$!
    trap 'if [ -n "$mock_pid" ]; then kill "$mock_pid" 2>/dev/null || true; fi' EXIT
    sleep 0.6
  else
    exec 3>&-
    echo "Port 8082 belegt, nutze den laufenden Server."
  fi
fi

"$here/sim/run.sh" "$@"
