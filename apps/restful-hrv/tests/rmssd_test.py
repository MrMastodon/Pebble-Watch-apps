#!/usr/bin/env python3
"""Checks the worker's RMSSD against an independent floating-point reference.

The C under test is not copied: prv_isqrt(), prv_within_tolerance() and
prv_compute_rmssd_ms() are cut out of worker_src/c/worker.c at run time and
compiled for the host with gcc, next to a small harness that feeds them
intervals. So what is tested is exactly what ships.

The reference below implements the same specification in floats - the Malik
20 % filter against the last accepted interval, the anchor at the first pair
that agrees, and the floor of 10 accepted intervals - and rounds the true
square root to the nearest millisecond.

Usage: python3 tests/rmssd_test.py   (needs gcc)
"""
import math
import os
import random
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
WORKER = os.path.join(APP, 'worker_src', 'c', 'worker.c')
HEADER = os.path.join(APP, 'src', 'common', 'hrv_common.h')

MIN_VALID = 10
TOL_PCT = 20
CAPACITY = 300


def extract_c():
    src = open(WORKER).read()
    start = src.index('static uint32_t prv_isqrt(')
    end = src.index('// Static rather than on the stack.')
    body = src[start:end]
    # The functions read the worker's buffer; give them the same globals.
    assert 'prv_compute_rmssd_ms' in body
    return body


HARNESS = r'''
#include <stdio.h>
#include <stdbool.h>
#include "%(header)s"
#define PPI_BUFFER_SIZE HRV_PPI_CAPACITY
#define MIN_VALID_SAMPLES %(min_valid)d
static uint16_t s_ppi_buffer[PPI_BUFFER_SIZE];
static uint16_t s_ppi_count;
%(body)s
int main(void) {
  unsigned n;
  while (scanf("%%u", &n) == 1) {
    s_ppi_count = 0;
    for (unsigned i = 0; i < n; i++) {
      unsigned v; if (scanf("%%u", &v) != 1) return 1;
      if (s_ppi_count < PPI_BUFFER_SIZE) s_ppi_buffer[s_ppi_count++] = (uint16_t)v;
    }
    uint16_t rejected = 0;
    uint32_t r = prv_compute_rmssd_ms(&rejected);
    printf("%%u %%u\n", (unsigned)r, (unsigned)rejected);
  }
  return 0;
}
'''


def within(value, reference):
    return abs(value - reference) <= reference * TOL_PCT / 100.0


def reference(ppis):
    """Returns (rmssd_ms, rejected) as the specification describes them."""
    ppis = ppis[:CAPACITY]
    n = len(ppis)
    if n < MIN_VALID:
        return 0, 0
    start = 0
    while start + 1 < n and not within(ppis[start + 1], ppis[start]):
        start += 1
    if start + 1 >= n:
        return 0, n
    rejected = start
    accepted = [ppis[start]]
    for p in ppis[start + 1:]:
        if within(p, accepted[-1]):
            accepted.append(p)
        else:
            rejected += 1
    if len(accepted) < MIN_VALID:
        return 0, rejected
    diffs = [b - a for a, b in zip(accepted, accepted[1:])]
    value = math.sqrt(sum(d * d for d in diffs) / len(diffs))
    return int(math.floor(value + 0.5)), rejected


def cases():
    rnd = random.Random(20260929)
    out = []
    # Edge cases first.
    out.append([])
    out.append([800] * 9)                          # below the floor
    out.append([800] * 10)                         # flat: RMSSD 0 is "no result"
    out.append([800, 810] * 5)                     # exactly ten
    out.append([1600] + [800, 820] * 10)           # opens on an artefact
    out.append([800, 1600] * 10)                   # every other one doubled
    out.append([400, 900, 400, 900, 400, 900, 400, 900, 400, 900, 400])  # nothing agrees
    out.append([65535] * 12)                       # top of the range
    out.append([65535, 60000] * 8)                 # large differences
    out.append([1] * 12)                           # bottom of the range
    out.append([800 + (i % 7) * 9 for i in range(400)])   # more than the buffer holds
    # Generated nights: a drifting rhythm, beat-to-beat noise, and artefacts.
    for _ in range(3000):
        n = rnd.randint(5, 320)
        base = rnd.uniform(600, 1300)
        noise = rnd.uniform(2, 60)
        series = []
        for i in range(n):
            base += rnd.gauss(0, 3)
            v = base + rnd.gauss(0, noise)
            roll = rnd.random()
            if roll < 0.03:
                v *= 2          # a missed beat
            elif roll < 0.05:
                v *= 0.5        # an extra beat
            series.append(max(1, min(65535, int(round(v)))))
        out.append(series)
    return out


def main():
    with tempfile.TemporaryDirectory() as tmp:
        c_path = os.path.join(tmp, 'harness.c')
        exe = os.path.join(tmp, 'harness')
        open(c_path, 'w').write(HARNESS % {'header': HEADER, 'body': extract_c(),
                                           'min_valid': MIN_VALID})
        subprocess.check_call(['gcc', '-O2', '-Wall', '-Wno-unused-function', '-o', exe, c_path])
        all_cases = cases()
        stdin = ''.join('%d %s\n' % (len(c), ' '.join(map(str, c))) for c in all_cases)
        result = subprocess.run([exe], input=stdin, capture_output=True, text=True, check=True)
        got = [tuple(map(int, line.split())) for line in result.stdout.split('\n') if line]

    mismatches = []
    for series, (value, rejected) in zip(all_cases, got):
        want = reference(series)
        if (value, rejected) != want:
            mismatches.append((series[:12], len(series), (value, rejected), want))
    print('%d cases, %d mismatches' % (len(all_cases), len(mismatches)))
    for m in mismatches[:10]:
        print('  first values %s (n=%d): worker %s, reference %s' % m)
    return 1 if mismatches else 0


if __name__ == '__main__':
    sys.exit(main())
