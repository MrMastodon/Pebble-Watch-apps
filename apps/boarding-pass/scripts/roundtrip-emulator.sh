#!/usr/bin/env bash
# End-to-end round trip on the emery emulator, no hardware needed.
#
# Builds a throwaway copy of the watchapp with a PebbleKit JS sender bolted on,
# pushes a synthetic boarding pass over the same AppMessage keys the Android app
# uses, screenshots the watch and decodes the screenshot. If the decoded string
# matches the one that went in, then packing, the message format, unpacking and
# drawing are all correct together.
#
# The shipped app has no JS - the sender only ever exists in the temporary copy.
#
# Requires the Pebble SDK on PATH plus `pip install zxing-cpp numpy pillow`.
set -euo pipefail

app_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scripts="$app_root/scripts"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "==> Generating the test matrix"
python3 "$scripts/roundtrip.py" \
  --write-js "$work/index.js" \
  --write-text "$work/expected.txt"

echo "==> Building a test copy of the watchapp"
cp -r "$app_root/watchapp" "$work/testapp"
rm -rf "$work/testapp/build"
mkdir -p "$work/testapp/src/pkjs"
cp "$work/index.js" "$work/testapp/src/pkjs/index.js"

cd "$work/testapp"
pebble build
pebble install --emulator emery

# Give the JS runtime time to connect and deliver the message.
sleep 8

echo "==> Taking a screenshot"
pebble screenshot --emulator emery --no-open "$work/screenshot.png"

echo "==> Decoding the screenshot"
python3 "$scripts/roundtrip.py" \
  --check "$work/screenshot.png" \
  --text-file "$work/expected.txt"
