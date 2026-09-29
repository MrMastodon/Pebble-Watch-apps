#!/usr/bin/env bash
# Compiles the real worker against the stand-in SDK and runs every scenario.
#   tests/worker_sim/run.sh               all scenarios
#   tests/worker_sim/run.sh night -v      one scenario, with the worker's log
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="$here/../.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
gcc -std=gnu11 -O1 -Wall -Wno-unused-function -I"$here" -Dmain=worker_main -Wno-return-type -c "$app/worker_src/c/worker.c" -o "$out/worker.o" || exit 1
gcc -std=gnu11 -O1 -Wall -I"$here" -c "$here/sim.c" -o "$out/sim.o" || exit 1
gcc "$out/worker.o" "$out/sim.o" -o "$out/sim" || exit 1
if [ $# -gt 0 ]; then exec "$out/sim" "$@"; fi
failed=0
for s in night zero clock overlap capacity; do
  "$out/sim" "$s" || failed=$((failed + 1))
done
exit "$failed"
