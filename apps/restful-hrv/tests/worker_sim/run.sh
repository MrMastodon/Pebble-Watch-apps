#!/usr/bin/env bash
# Compiles the real worker against the stand-in SDK and runs one simulated night.
#   tests/worker_sim/run.sh          summary
#   tests/worker_sim/run.sh -v       with the worker's own log lines
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="$here/../.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
gcc -std=gnu11 -O1 -Wall -Wno-unused-function -I"$here" -Dmain=worker_main -Wno-return-type -c "$app/worker_src/c/worker.c" -o "$out/worker.o"
gcc -std=gnu11 -O1 -Wall -I"$here" -c "$here/sim.c" -o "$out/sim.o"
gcc "$out/worker.o" "$out/sim.o" -o "$out/sim"
"$out/sim" "$@"
