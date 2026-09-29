#!/usr/bin/env bash
# Runs every check behind REVIEW.md. Needs gcc, python3 (3.9+) and node.
# The page fuzz test also needs Playwright with a Chromium; it is skipped,
# and says so, when that is not installed.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="$here/.."
failed=0

step() {
  echo
  echo "==> $1"
  shift
  if "$@"; then echo "    passed"; else echo "    FAILED"; failed=$((failed + 1)); fi
}

step "RMSSD in the worker vs the reference" python3 "$here/rmssd_test.py"
step "The real worker through six simulated scenarios" "$here/worker_sim/run.sh"
step "Settings page statistics vs the reference" env TZ=Europe/Oslo node "$here/page_stats.test.js"
step "Settings page decoding and CSV" node "$here/decode.test.js"

if node -e "try{require('playwright')}catch(e){require(require('child_process').execSync('npm root -g').toString().trim()+'/playwright')}" 2>/dev/null; then
  step "Settings page against hostile links" node "$here/fuzz_page.js"
else
  echo
  echo "==> Settings page against hostile links: skipped (Playwright not installed)"
fi

echo
if [ "$failed" -eq 0 ]; then echo "All checks passed."; else echo "$failed check(s) failed."; fi
exit "$failed"
