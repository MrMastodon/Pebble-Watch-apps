# Restful HRV — code review before publishing

| | |
|---|---|
| Version reviewed | 1.8.0 (commit `0ec6abf`) |
| Fixed in | 1.8.1 (`b961a1a`) and 1.9.0 (`9abd4b9` … `7b7233a`) |
| Date | 29 September 2026 |
| Perspectives | Security and privacy · Stability and robustness · Measurement correctness · Battery and performance |
| Scope | `worker_src/c/worker.c`, `src/c/main.c`, `src/common/hrv_common.h`, `src/pkjs/index.js`, `docs/restful-hrv/index.html`, `package.json`, `wscript`, `scripts/build-all.sh`, and what `README.md` and `appstore/listing.md` tell users |
| Not in scope | `apps/one-off-alarm`; the Pebble firmware and the Pebble mobile app, which this app relies on but cannot inspect |
| Checks | [`tests/`](tests/) — run them all with `tests/run.sh` |

## Summary

The review found no critical problems, one High finding and 17 lower ones:
- 15 are fixed.
- One is mitigated rather than fixed (SEC-6).
- Two are accepted as they are (CORR-1, STAB-8).

Every fix is backed by a check that runs without a watch, or by review where
none is possible, and the whole suite passes on 1.9.0. Three of the problems
(STAB-1, STAB-3, STAB-4) were first reproduced in the simulator: the checks
for them fail on the old code and pass on the new.

The most important changes:

- **Flash writes overnight (BATT-1, High):** the worker wrote its diagnostics
  to flash every minute of the night, and every second during a measurement.
  A simulated night went from 917 writes to 58.
- **Data logging removed (SEC-2):** every measurement was also sent to
  Pebble's data logging, which nothing reads. It is gone.
- **A month of history (STAB-7):** the watch kept only 32 measurements, and
  eight nights without opening the app lost the oldest. It now keeps 96–128,
  about a month.
- **A clock set back (STAB-4):** this held the sensor for 719 seconds. The
  window is now bounded whatever the clock does.
- **Honest failure messages (STAB-1, STAB-2):** the watch blamed the strap, or
  said "switched off", when neither was the problem.
- **Settings page hardening (SEC-3 to SEC-6):** a Content-Security-Policy,
  refusal of unknown link formats, a clipboard warning, and an honest message
  when delete is tapped outside the Pebble app.
- **The store listing (SEC-1)** now says only what can be verified.

What held up from the start:

- **The settings page runs no code from a link.** Links are untrusted input:
  anyone can craft one.
- **The RMSSD arithmetic** agrees exactly with an independent reference.
- **The sensor is released** on every path out of a measurement.
- **The nightly statistics** agree with a separate implementation of their
  definitions.

**Not verified here:** no watch was available, and the emulator does not run
in the review environment. Everything the watch does has been checked in a
simulator that compiles the real worker code. Some things only a real Pebble
Time 2 can confirm; they are listed under [Limitations](#limitations).

## How data moves (1.9.0)

```
 heart rate sensor
        │ HRV readings (health events)
        ▼
 background worker
        │ persist: up to 128 measurements (4 × 256-byte chunks) + diagnostics
        ▼
 watch app (only while open)
        │ AppMessage: one message per 32-record chunk, diagnostics in the first
        ▼
 PebbleKit JS, inside the Pebble mobile app
        │ localStorage: up to 500 measurements + diagnostics
        ▼
 settings page (static file on GitHub Pages)
        measurements travel in the URL fragment (#...), which the browser never
        sends to GitHub. GitHub sees that the page was opened, not what is in it.
        A Content-Security-Policy stops the page loading or sending anything.
```

What the page can send back is limited to one request: delete. It goes to
PebbleKit JS by `pebblejs://close`, and only the `clear` field is acted on. The
phone drops its copy only after the watch confirms that it has forgotten its
own.

Builds before 1.9 also sent every measurement to Pebble's data logging (tags
`HRV1`, `HRV2`). 1.9.0 no longer does.

## Findings

Severity:
- **Critical:** data loss, a crash or hang on the watch, code executing from a
  link, or a false privacy claim.
- **High:** a likely malfunction in normal use, or a large and avoidable
  battery or flash cost.
- **Medium:** an edge case with visible impact, or a robustness gap.
- **Low:** hardening or clarity.
- **Info:** checked and worth recording.

| ID | Severity | Where | Finding | Status | Checked by |
|---|---|---|---|---|---|
| BATT-1 | High | `worker.c` `prv_evaluate_sleep_state()` | Diagnostics were written to flash on every evaluation while asleep. | **Fixed** `b961a1a` | `worker_sim` night |
| SEC-1 | Medium | `appstore/listing.md` | The store listing was out of date and claimed more about privacy than can be verified. | **Fixed** `7b7233a` | Review of the text |
| SEC-2 | Medium | `worker.c` | Every measurement was sent to data logging, which nothing reads. | **Fixed** `9abd4b9` (removed) | `worker_sim`: nothing logged |
| STAB-1 | Medium | `worker.c` `prv_stop_measurement()` | "No intervals" was decided from an all-time counter. | **Fixed** `35802aa` | `worker_sim` zero |
| STAB-2 | Medium | `main.c` Measure now | "Measuring is off" was shown whenever the worker was not running. | **Fixed** `e7977d0` | Needs hardware check |
| STAB-3 | Low | `worker.c` `prv_evaluate_sleep_state()` | A Measure now used up the trigger of a restful episode beginning during it. | **Fixed** `35802aa` | `worker_sim` overlap |
| STAB-4 | Low | `worker.c` `prv_tick_handler()`, `main.c` | A clock set back stretched the window and held the sensor. | **Fixed** `35802aa`, `e7977d0` | `worker_sim` clock |
| STAB-5 | Low | `hrv_common.h`, `worker.c` | Diagnostic counters wrapped at 65,535. | **Fixed** `35802aa`, `c1ac8f3` | Review of the code |
| STAB-6 | Low | `worker.c` `prv_append_history()` | On inconsistent storage the worker dropped the whole history. | **Fixed** `593ef33` | `worker_sim` corrupt |
| STAB-7 | Low | design | 32 measurements on the watch; about eight nights without opening the app lost the oldest. | **Fixed** `593ef33` (96–128) | `worker_sim` capacity; sending needs hardware check |
| SEC-3 | Low | `index.html` `render()` | Unknown link versions were read as the old format. | **Fixed** `c1ac8f3` | `decode.test.js`, `fuzz_page.js` |
| SEC-4 | Low | `index.html` | No Content-Security-Policy. | **Fixed** `c1ac8f3` | `fuzz_page.js` (no violations) |
| SEC-5 | Low | `index.html` export card | Copying put health data on the clipboard without saying so. | **Fixed** `c1ac8f3` | Review of the page |
| SEC-6 | Low | `index.html` `inPebbleApp()` | Any Android WebView is treated as the Pebble app. | **Mitigated** `c1ac8f3` | Playwright: fallback message |
| BATT-2 | Low | `worker.c` `prv_tick_handler()` | Measure now wrote the diagnostics every second. | **Fixed** `35802aa` (every 2 s) | `worker_sim` night |
| DOC-1 | Low | `index.js` | A comment said the watch keeps 40 measurements. | **Fixed** `593ef33` | — |
| CORR-1 | Info | `worker.c` `prv_compute_rmssd_ms()` | A perfectly flat series gives 0, which is treated as no result. | Accepted | `rmssd_test.py` |
| STAB-8 | Info | `index.js` `mergeRecords()` | Records are merged by timestamp. | Accepted | — |

### BATT-1 — flash writes all night (High, fixed in `b961a1a`)

`prv_evaluate_sleep_state()` stored the "last seen asleep / restful" timestamps
and wrote the diagnostics to flash whenever either bit was set. It runs on
every tick: once a minute while idle and once a second during a measurement. It
also runs on sleep and significant health events. The code already had a
15-minute heartbeat for exactly this kind of field, and its own comment said
that a write every minute all night would be a real cost.

**Fix:** the timestamps are still updated in RAM on every evaluation, but the
worker writes only when sleep or restful sleep starts or stops. The heartbeat
keeps the stored copy at most 15 minutes behind in between. "Last seen asleep"
is exact in the morning, because it is written at the moment sleep ends.

**Evidence:** `tests/worker_sim` runs the real `worker.c` through a night:
asleep 23:10–06:50, four restful episodes. Before the fix it came to 917
diagnostic writes; after the fix, 58. The measurements were identical. The
scenario has since gained two Measure now requests. In 1.9.0 it comes to 136
diagnostic writes, of which about 75 are Measure now's progress writes
(BATT-2). The scenario fails if the worker goes back to writing on every tick.

### SEC-1 — store listing (Medium, fixed in `7b7233a`)

**The problem:** the listing still described version 1.1:
- a per-measurement table;
- "last 32" in its description;
- nothing about nights, Measure now or the diagnostics.

It also ended its privacy paragraph with "nothing is uploaded anywhere", while
the data passes through the Pebble mobile app, whose handling this project
cannot see.

**The fix:** the listing is rewritten for 1.9.0. Its privacy paragraph now says
only what the code shows:
- this app sends the measurements to no server;
- GitHub sees only that the page was opened;
- the measurements pass through the Pebble app;
- anyone given the link can read them.

The earlier release notes are kept verbatim as history.

### SEC-2 — data logging (Medium, fixed in `9abd4b9`)

Every measurement was also logged with `data_logging_log()` under `HRV2`.
PebbleKit JS cannot read data logging, and there is no native companion app,
so nothing read it. It was health data handed to a channel whose retention
and forwarding are up to the Pebble mobile app.

**The fix:** it is removed from the worker. The README, the delete note on the
settings page and the listing say that earlier builds logged there, and that
this app can no longer reach or add to those records.

### STAB-1 — "no intervals" (Medium, fixed in `35802aa`)

**The problem:** a failed measurement was labelled "no intervals" (the watch
did not think it was worn) when it collected nothing *and* an all-time counter
of empty readings was above zero. After the first empty reading ever, a
measurement that got no readings at all was blamed on the strap, both in the
diagnostics and on Measure now's result screen.

**The fix:** the counter is noted when a measurement starts, and the result is
compared with that. The `zero` scenario first runs a Measure now where every
reading is empty (expect "no intervals"), then one where no readings arrive at
all (expect "too few"). On the old code the second also said "no intervals".

### STAB-2 — "Measuring is off" (Medium, fixed in `e7977d0`)

**The problem:** Measure now decided between its introduction and "Measuring
is off" from `app_worker_is_running()` alone. The worker can be stopped while
measuring is switched on:
- just after it was switched on;
- while the system asks whether to replace another app's worker;
- after another app's worker has taken the single background slot.

**The fix:** "Measuring is off" now depends on the setting. When measuring is
on and the worker is not running, a new screen says exactly that.

**Needs hardware check:** this is app UI and cannot run in the simulator.

### STAB-3 to STAB-7

- **STAB-3 (fixed):** the worker no longer updates its "was restful" state
  during a Measure now. A restful episode that begins while one is running
  gets its own measurement afterwards. The `overlap` scenario checks it: two
  measurements, one manual. The old code gave one.
- **STAB-4 (fixed):**
  - *The problem:* the window was timed only by `time()`. A clock correction
    from the phone that set it back ten minutes held the sensor for 719
    seconds in the `clock` scenario.
  - *The fix:* the window is also capped at 125 second ticks, and a clock that
    has gone backwards counts as elapsed. The sensor is now released at once,
    and a partial measurement is kept if it has enough intervals.
  - *On the app side,* the countdown is clamped to 0–2:00.
- **STAB-5 (fixed):** diagnostic counters saturate at 65,535 instead of
  wrapping, through a helper that takes and returns the value; a pointer into
  the packed struct could be unaligned. The settings page shows such a counter
  as "65,535+".
- **STAB-6 (fixed):** when the count and the stored records disagree, the
  worker keeps the records it can read, as the app always did. The old code
  started again from nothing, which is clear from reading it. In the `corrupt`
  scenario, 40 records with a chunk cut to 3, the new code keeps the 35 that
  can be read and adds the next.
- **STAB-7 (fixed):** the history is four persist chunks of 32 records. Chunk
  0 is the old single key, so existing histories are read unchanged.
  - An ordinary append rewrites one chunk and the count.
  - When the history is full, the oldest whole chunk is dropped at once, so
    the rewrite of every chunk happens only once every 32 measurements.
  - The app sends one chunk per message, chaining on each acknowledgement.
    pkjs already merged message by message, so the protocol did not change.
  - The `capacity` scenario runs 140 Measure nows: it expects 108 kept, oldest
    first, and at most two writes per ordinary append.
  - *Needs hardware check:* the chained sending. Code review found and fixed
    one problem in it before release: a second send trigger could have ended a
    transfer under way.

### Settings page (SEC-3 to SEC-6)

- **SEC-3 (fixed):** only `v=1` (six-byte records) and `v=2` (eight-byte) are
  read. Git history shows every phone-side build sent one of the two. Anything
  else shows "Newer data than this page understands" instead of nonsense.
- **SEC-4 (fixed):** a Content-Security-Policy allows only the page's own
  inline script and style, plus `data:` for the CSV:
  `default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; base-uri 'none'; form-action 'none'`.
  - The fuzz test fails on any violation, so the policy cannot silently break
    the page.
  - An image from another site, injected on purpose, was reported, so the
    test would catch a real violation.
  - The CSV download still works under the policy.
- **SEC-5 (fixed):** the export card says that copying puts the measurements
  on the clipboard, where other apps may be able to read them.
- **SEC-6 (mitigated):** telling the Pebble app's web view apart from other
  Android web views depends on a bridge object whose presence could not be
  confirmed on every phone. Narrowing the detection could therefore have
  broken delete in the real app, so it was left alone. Instead, if the page is
  still open two seconds after a delete, it says "Not deleted" and explains
  that deleting only works inside the Pebble app. Before, it sat on
  "Deleting…".

### BATT-2, DOC-1, and what is accepted

- **BATT-2 (fixed):** during Measure now the worker writes its diagnostics
  every 2 s instead of every second: 60 writes for a full Measure now instead
  of 120. The app still polls every second, so the countdown stays smooth.
- **DOC-1 (fixed):** the comment in `src/pkjs/index.js` gives the real
  capacity.
- **CORR-1 (accepted):** a series of identical intervals has an RMSSD of 0,
  which the worker treats as "no result". It cannot happen with a real heart,
  and treating it as a failure is the safe reading.
- **STAB-8 (accepted):** the phone merges incoming records by timestamp, so
  two records can only collide if they ended in the same second. The worker
  cannot produce that; only a watch whose clock was reset could.

## What was checked, by perspective

### Security and privacy

- **The settings page against hostile links** (`tests/fuzz_page.js`).
  Seventeen kinds of link, each opened as in the Pebble app and in a plain
  browser: 34 runs. The cases:
  - script in every field of the status JSON;
  - script in the timestamp and in the data;
  - JSON that does not parse, a broken escape, arrays and numbers instead of
    objects;
  - unknown and missing versions, and random bytes;
  - timestamps at 0 and 2³²−1;
  - a 200 kB payload.

  In all 34, nothing ran, no dialog opened, no image was created, no request
  left the page, there was no CSP violation, and the page rendered.
- **Where data can enter the page.** `innerHTML` is only ever used to clear an
  element. There is no `eval`, no `new Function`, and no `document.write`.
  The only link the page builds is the CSV `data:` URI, from numbers.
- **PebbleKit JS.**
  - It keeps records, status and a pending-delete flag in `localStorage`, with
    every access wrapped.
  - It decodes watch bytes with bounds checks.
  - It honours only `clear` from the page.
  - Its only network action is `Pebble.openURL` to the page.
- **Deleting is two-phase.** The phone keeps its copy until the watch confirms.
  History arriving while a delete is pending is ignored. The watch now deletes
  all four history chunks.
- **Least privilege.** `package.json` asks for `health` and `configurable`
  only. There is no location, and no network from the watch.
- **Claims.** The page, the READMEs and the store listing were each checked
  against the code.

### Stability and robustness

- **The real worker in six simulated scenarios** (`tests/worker_sim`):
  - *night:* four restful episodes, one ending inside the window, plus one
    Measure now left to finish and one cancelled. Five measurements are stored,
    one flagged manual; the cancelled one stores nothing; the sensor is held
    for exactly 4 × 120 + 60 + 30 seconds; nothing goes to data logging; the
    flash budget holds.
  - *zero:* empty readings give "no intervals"; no readings at all give "too
    few" (STAB-1).
  - *clock:* with the clock set back ten minutes, the sensor is released within
    the window (STAB-4).
  - *overlap:* restful sleep beginning during a Measure now still gets its own
    measurement (STAB-3).
  - *capacity:* 140 Measure nows keep 108, oldest first, with two writes per
    ordinary append (STAB-7).
  - *corrupt:* a history shorter than its count keeps what is readable
    (STAB-6).

  In every scenario, every persistent write is checked to be at most 256
  bytes, and the sensor must be released at the end.
- **The sensor is released on every exit path.** In the simulator: the end of
  the window, the end of an episode, cancel, and the clock going back. By
  reading the code: switching off and `prv_deinit()`. `prv_init()` also
  releases it in case a previous run was killed.
- **Worker memory.** 5,292 of 10,240 bytes, from the build report. That
  includes the 1 KB history buffer, which is static rather than on the stack.
  The worker binary contains no floating-point routines, only the 64-bit
  integer division helper, checked with `arm-none-eabi-nm` on 1.9.0.
- **The watch app.**
  - Every timer is cancelled when its window unloads.
  - The phone message still fits its outbox: 307 of 388 bytes, now per chunk.
  - A failed send ends the transfer and gets one retry, which starts again
    from the first chunk.
- **Time.**
  - Nights on the page run noon to noon in local time, including across both
    DST changes, month ends and year ends.
  - The worker's window follows UTC and is capped in ticks (STAB-4).
- **Build.** A clean `pebble build` of 1.9.0 gives no warnings other than the
  SDK linker's standard note about RWX segments, which the unchanged code
  also gets.

### Measurement correctness

- **RMSSD** (`tests/rmssd_test.py`). The worker's own `prv_isqrt()`,
  `prv_within_tolerance()` and `prv_compute_rmssd_ms()` are cut out of
  `worker.c` and compiled for the host unchanged. They are compared with a
  floating-point implementation of the same specification: the Malik 20 %
  filter against the last accepted interval, the anchor at the first agreeing
  pair, at least 10 accepted intervals, rounding to the nearest millisecond.

  Across 3,011 cases the two agree exactly, on both the RMSSD and the number
  rejected. The cases:
  - 11 edge cases: empty, below the floor, exactly ten, opening on an
    artefact, alternating doubled beats, nothing agreeing, the top and bottom
    of the 16-bit range, more intervals than the buffer holds;
  - 3,000 generated series with drifting rhythm, noise, and 5 % missed or
    extra beats.
- **Nightly statistics** (`tests/page_stats.test.js`). The page's own
  functions agree with `tests/ref_stats.py`, written separately from the
  README's definitions, on synthetic histories of 1 to 40 nights. That covers
  nights, medians, the 7- and 30-night windows, the baseline that leaves the
  latest night out, standard deviation and CV with n−1, the minimum-night
  rules, the middle measurements behind a median, and Measure now readings
  never reaching a night.
- **Link decoding** (`tests/decode.test.js`):
  - the manual flag and the artefact count stay separate;
  - timestamps after 2038 stay positive;
  - old six-byte records have no count;
  - a trailing partial record is ignored;
  - junk characters are skipped;
  - only versions 1 and 2 are read;
  - the CSV has one row per measurement.

### Battery and performance

- **Sensor time.** The sensor runs at one reading a second only during a
  measurement: 120 seconds per restful episode, or per Measure now. That is 8
  to 10 minutes on a typical night of 4 to 5 episodes. The rest of the time
  the worker asks for no sample period at all.
- **Wake-ups.** The worker ticks once a minute when idle and once a second only
  inside a measurement. The system's HRV broadcast, about 2,000 a day, wakes it
  briefly to count.
- **Flash.**
  - About 60 writes a night: diagnostics at sleep changes, at the start and end
    of measurements, and every 15 minutes, plus two per stored measurement.
  - A Measure now adds about 60.
  - Once every 32 measurements the history shift rewrites four chunks.
- **Phone.** Opening the app sends up to four messages of at most 307 bytes.
  The settings link is at most about 6 kB.

## Re-check after fixes

`tests/run.sh` on 1.9.0 (`7b7233a`):

```
==> RMSSD in the worker vs the reference
3011 cases, 0 mismatches
==> The real worker through six simulated scenarios
scenario: night     5 stored (1 manual), sensor 570 s, released; all checks passed
scenario: zero      strap off -> no intervals, no readings -> too few; all checks passed
scenario: clock     sensor released 30 s after the start; all checks passed
scenario: overlap   2 stored (1 manual); all checks passed
scenario: capacity  kept 108 of 140, at most 2 writes per append; all checks passed
scenario: corrupt   count said 40, chunk cut to 3 -> 36 kept; all checks passed
==> Settings page statistics vs the reference
13 tests passed
==> Settings page decoding and CSV
decode tests passed
==> Settings page against hostile links
34 runs, no failures
All checks passed.
```

## Limitations

- **No hardware.** No watch was available, and the emulator does not run in
  the review environment. These need a real Pebble Time 2 before release:
  - Measure now in all its states: countdown, result, cancel, off, not
    running, busy;
  - sending a full history as four chained messages, and the phone receiving
    all of them;
  - an existing 1.8 history being read after upgrading to 1.9;
  - deleting, which must clear all four chunks;
  - battery over a real night.
- **The simulator is a model.** It stands in for the SDK, and its nights are
  invented. Real health events arrive on the firmware's own schedule, so the
  exact counts will differ.
- **The Pebble mobile app is outside this review.** Its handling of AppMessage
  and `localStorage` could not be checked. Nor could what it does with data
  logging records from builds before 1.9.

## Rerunning the checks

```sh
apps/restful-hrv/tests/run.sh
```

Needs `gcc`, `python3` 3.9+ and `node`. The fuzz test also needs Playwright,
and is skipped otherwise. See [`tests/README.md`](tests/README.md) for what each
check does. No real measurements are kept in the repository: all test data is
synthetic.
