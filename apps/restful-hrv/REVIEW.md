# Restful HRV — code review before publishing

| | |
|---|---|
| Version reviewed | 1.8.0 (commit `0ec6abf`), fixes up to 1.8.1 (commit `b961a1a`) |
| Date | 29 September 2026 |
| Perspectives | Security and privacy · Stability and robustness · Measurement correctness · Battery and performance |
| Scope | `worker_src/c/worker.c`, `src/c/main.c`, `src/common/hrv_common.h`, `src/pkjs/index.js`, `docs/restful-hrv/index.html`, `package.json`, `wscript`, `scripts/build-all.sh`, and what `README.md` and `appstore/listing.md` claim |
| Not in scope | `apps/one-off-alarm`; the Pebble firmware and the Pebble mobile app, which this app relies on but cannot inspect |
| Checks | [`tests/`](tests/) — run them all with `tests/run.sh` |

## Summary

No critical findings. One finding was **High** and has been fixed: the
background worker wrote its diagnostics to flash every minute of the night, and
every second during a measurement, about 900 writes a night. It now writes
only when sleep starts or stops, plus a 15-minute heartbeat. A simulated night
went from 917 writes to 58, with the same measurements (`b961a1a`).

The remaining findings are **Medium** or lower and are listed as
recommendations. Two of them should be dealt with before publishing, because
they concern what users are told:

- The store listing is out of date and says more about privacy than this
  project can verify (SEC-1).
- Every measurement also goes to Pebble's data logging, which nothing in this
  project reads and whose handling on the phone cannot be checked (SEC-2).

The parts that carry the most risk held up under test:

- **The settings page runs no code from a link.** Links are untrusted input:
  anyone can craft one. Script placed in every field ran nowhere, broken input
  crashed nothing, and nothing was fetched from anywhere.
- **The RMSSD arithmetic agrees with an independent reference.** The worker's
  integer version matched a floating-point reference in all 3,011 test cases.
- **The sensor is always released.** Every path that ends a measurement gives
  the heart rate sensor back.
- **The nightly statistics on the page agree with a separate implementation
  of their definitions.**

**Not verified here:** the review was done without a watch or a working
emulator. Measure now (new in 1.8.0) and the flash fix (1.8.1) have been
exercised in the simulator and the page in a browser. Neither has run on a Pebble
Time 2 yet. They are marked *needs hardware check* below.

## How data moves

```
 heart rate sensor
        │ HRV readings (health events)
        ▼
 background worker ──► DataLogging, tag HRV2 ──► Pebble mobile app (not read by this project)
        │ persist: last 32 measurements + diagnostics
        ▼
 watch app (only while open)
        │ AppMessage: history + diagnostics
        ▼
 PebbleKit JS in the Pebble mobile app
        │ localStorage: up to 500 measurements + diagnostics
        ▼
 settings page (static file on GitHub Pages)
        measurements travel in the URL fragment (#...), which the browser never
        sends to GitHub. GitHub sees that the page was opened, not what is in it.
```

What the page can send back is limited to one request: delete. It goes to
PebbleKit JS by `pebblejs://close`, and only the `clear` field is acted on. The
phone drops its copy only after the watch confirms that it has forgotten its
own.

## Findings

Severity:
- **Critical:** data loss, a crash or hang on the watch, code executing from a
  link, or a false privacy claim.
- **High:** a likely malfunction in normal use, or a large and avoidable
  battery or flash cost.
- **Medium:** an edge case with visible impact, or a robustness gap.
- **Low:** hardening or clarity.
- **Info:** checked and worth recording.

Line numbers refer to commit `b961a1a`.

| ID | Severity | Where | Finding | Status |
|---|---|---|---|---|
| BATT-1 | High | `worker.c` `prv_evaluate_sleep_state()` | Diagnostics were written to flash on every evaluation while asleep: every minute all night, every second during a measurement. | **Fixed** in `b961a1a`. Simulated; needs hardware check |
| SEC-1 | Medium | `appstore/listing.md` | The store listing is out of date and its privacy statement is broader than can be verified. | Open — update before publishing |
| SEC-2 | Medium | `worker.c:324`, `worker.c:471` | Every measurement is sent to data logging, which nothing here reads and whose handling on the phone is unknown. | Open |
| STAB-1 | Medium | `worker.c:308` | "No intervals" is decided from an all-time counter, not from this measurement. | Open |
| STAB-2 | Medium | `main.c:761`, `main.c:576` | Measure now says "Measuring is off" whenever the worker is not running, including when it is switched on. | Open |
| STAB-3 | Low | `worker.c:368` | A Measure now running when restful sleep begins uses up that episode's trigger. | Open |
| STAB-4 | Low | `worker.c:397`, `main.c:507` | The measurement window follows the wall clock; a clock set backwards stretches it. | Open |
| STAB-5 | Low | `hrv_common.h:128` | The all-day HRV reading counter wraps after about a month. | Open |
| STAB-6 | Low | `worker.c:197` | On inconsistent storage, the worker drops the whole on-watch history. The app keeps what it can read. | Open |
| STAB-7 | Low | design | Measurements reach the phone only when the app is opened. About eight nights without opening it loses the oldest ones. | Open (documented in README) |
| SEC-3 | Low | `index.html:720` | Any link version other than `2` is read as the old six-byte format. | Open |
| SEC-4 | Low | `index.html` | No Content-Security-Policy. | Open |
| SEC-5 | Low | `index.html:938` | The copy buttons put health data on the clipboard without saying so. | Open |
| SEC-6 | Low | `index.html:238` | Any Android WebView is treated as the Pebble app. | Open |
| BATT-2 | Low | `worker.c:392` | Measure now writes the diagnostics every second, 120 writes each time. | Accepted for now |
| DOC-1 | Low | `index.js:70` | A comment says the watch keeps 40 measurements. It keeps 32. | Open |
| CORR-1 | Info | `worker.c` `prv_compute_rmssd_ms()` | A perfectly flat series gives 0, which is treated as no result. | Accepted |
| STAB-8 | Info | `index.js:86` | Records are merged by timestamp, so a record with the same second replaces the stored one. | Accepted |

### BATT-1 — flash writes all night (High, fixed)

`prv_evaluate_sleep_state()` stored the "last seen asleep / restful" timestamps
and wrote the diagnostics to flash whenever either bit was set. It runs on
every tick: once a minute while idle and once a second during a measurement. It
also runs on sleep and significant health events. The code already had a
15-minute heartbeat for exactly this kind of field, and its own comment says
that a write every minute all night would be a real cost.

**Evidence:** `tests/worker_sim` runs the real `worker.c` through a night: asleep
23:10–06:50, four restful episodes. Before the fix it came to 917 diagnostic
writes; after the fix, 58. The stored measurements were identical.

The simulator in the repository has since gained two Measure now requests. With
those it gives 1,071 writes before the fix and 212 after. Of those 212, 150 are
Measure now's deliberate once-a-second writes (BATT-2).

**Fix:** the timestamps are still updated in RAM on every evaluation, but the
worker writes only when sleep or restful sleep starts or stops. The heartbeat
keeps the stored copy at most 15 minutes behind in between. The morning
reading of "last seen asleep" is exact, because it is written at the moment
sleep ends.

The simulator now fails if the night goes over its flash budget, so a return to
writing on every tick is caught.

**Needs hardware check:** that battery use over a night is unchanged or better.

### SEC-1 — store listing out of date (Medium, open)

`appstore/listing.md` still describes version 1.1:
- It gives the version as 1.1.
- Its release notes say the watch keeps "your last 40 measurements". It keeps
  32.
- It describes a per-measurement table and chart.
- It says nothing about nights, medians, Measure now or the diagnostics.

Its privacy sentence ends "so nothing is uploaded anywhere". That is true of
this app's own code: the page makes no requests, and PebbleKit JS talks to no
server. But the data also passes through the Pebble mobile app, by AppMessage,
`localStorage` and data logging, and this project cannot see what that app
does with it.

**Recommendation:** update the listing to 1.8.x before publishing. Use the
precise wording the settings page already uses: measurements are never sent
to GitHub or to any server by this app; GitHub sees only that the page was
opened; the link itself holds the measurements.

### SEC-2 — data logging nobody reads (Medium, open)

Every measurement is also logged with `data_logging_log()` under tag `HRV2`.
PebbleKit JS cannot read data logging, and this project has no native
companion app, so nothing reads it. It was added so a developer could pull the
raw record themselves.

It is health data handed to a channel whose retention and forwarding is up to
the Pebble mobile app. That goes against data minimisation, and it is
invisible to users unless they read the README.

**Recommendation:** remove it, make it opt-in, or at least say plainly in the
listing that measurements are also passed to the Pebble app's data logging.

### STAB-1 — "no intervals" from an all-time counter (Medium, open)

A failed measurement is labelled "no intervals" (the watch did not think it was
worn) when it collected nothing *and* `hrv_zero_events > 0`. That counter is
cumulative and never reset. So after the first empty reading ever, every
measurement that collects nothing gets that label, even one where no readings
arrived at all.

This shows up in the diagnostics. It also shows up in Measure now's result,
which then tells the user to check the strap when the strap may not be the
problem.

**Recommendation:** note the counter when a measurement starts, and compare
with that.

### STAB-2 — "Measuring is off" when it is not (Medium, open)

The Measure now screen decides between its introduction and "Measuring is off"
from `app_worker_is_running()`. The worker can be stopped while measuring is
switched on:
- just after it was switched on;
- while the system's "replace background app?" prompt is open;
- when another app's worker has taken the single background slot.

The screen then tells the user to switch on something that is already on.

**Recommendation:** check the stored setting as well, and say "The background
worker is not running" when it is on but stopped.

### Lower-severity findings

- **STAB-3.** The worker notes restful sleep as seen even while a Measure now
  is running. If restful sleep begins during one, that episode's start is used
  up and it gets no measurement of its own. It is rare, but it could be avoided
  by leaving `s_was_restful` untouched during a manual measurement.
- **STAB-4.** The two-minute window is measured against `time()`. The watch
  keeps UTC, so time zones and DST do not affect it, but a clock correction
  from the phone during a measurement shortens or stretches it. Set backwards,
  the sensor is held until the clock catches up, and Measure now's countdown
  shows more than 2:00. Capping the window by a tick count as well would bound
  it.
- **STAB-5.** `hrv_events` counts every HRV reading all day, about 2,000 a day
  in practice. As a `uint16_t` it wraps after about a month, and the figure in
  the diagnostics is then misleading. The other counters take months to years
  to wrap. The Measure now deltas are computed modulo 2¹⁶, so they survive a
  wrap.
- **STAB-6.** If the stored history is shorter than its count says,
  `prv_append_history()` starts from empty and the on-watch history is lost.
  The app, reading the same keys, keeps the records that are there, and the
  worker should do the same. The phone keeps its own copy, so this does not
  lose data the phone already has.
- **STAB-7.** The watch holds 32 measurements and sends them only while the app
  is open. The worker has no way to reach the phone. At three to four episodes a
  night, about eight nights without opening the app pushes the oldest ones out
  before the phone has them. The README says this. A reminder on the watch
  would make it visible.
- **SEC-3.** The page reads any version other than `v=2` as the old six-byte
  format, so a future format would be shown as garbage instead of being
  refused. It should refuse unknown versions.
- **SEC-4.** The page has no Content-Security-Policy. Injection was not
  possible in testing, since every value is inserted as text. A
  `<meta http-equiv="Content-Security-Policy">` allowing only its own inline
  script and style, and `data:` for the CSV, would make that hold even if a
  future change slipped.
- **SEC-5.** *Copy as CSV* and *Copy link for browser* put the measurements on
  the clipboard, where some phones let other apps read them. The page explains
  that the link stays in browser history, but not this.
- **SEC-6.** `inPebbleApp()` treats any Android WebView as the Pebble app.
  Opened inside another app's in-app browser, the page shows the delete button,
  which does nothing there.
- **BATT-2.** During Measure now the worker writes the diagnostics every second,
  so the app can show progress: 120 writes per use. That is acceptable for
  something a user starts by hand. Writing every two to three seconds would
  still show progress.
- **DOC-1.** A comment in `src/pkjs/index.js` says the watch keeps its last 40.
  It keeps 32.
- **CORR-1.** A series of identical intervals has an RMSSD of 0, which the
  worker treats as "no result". It cannot happen with a real heart.
- **STAB-8.** The phone merges incoming records by timestamp, so two
  measurements can only collide if they end in the same second, which the
  worker cannot produce. A watch whose clock was reset could produce it.

## What was checked, by perspective

### Security and privacy

- **The settings page against hostile links** (`tests/fuzz_page.js`). Sixteen
  kinds of link, each opened as in the Pebble app and in a plain browser: 32
  runs. The cases:
  - script in every field of the status JSON;
  - script in the timestamp and in the data;
  - JSON that does not parse, a broken escape, arrays and numbers instead of
    objects;
  - unknown versions and random bytes;
  - timestamps at 0 and 2³²−1;
  - a 200 kB payload.

  In all 32, nothing ran, no dialog opened, no image was created, no request
  left the page, and the page rendered.
- **Where data can enter the page.** `innerHTML` is only ever used to clear an
  element. There is no `eval`, no `new Function`, and no `document.write`.
  The only link the page builds is the CSV `data:` URI, from numbers.
- **PebbleKit JS.**
  - It keeps records, status and a pending-delete flag in `localStorage`, with
    every access wrapped.
  - It decodes watch bytes with bounds checks.
  - It honours only `clear` from the page.
  - Its only network action is `Pebble.openURL` to the page. It sends nothing
    anywhere itself.
- **Deleting is two-phase.** The phone keeps its copy until the watch confirms.
  History arriving while a delete is pending is ignored, so a half-finished
  delete leaves data on both sides rather than on neither.
- **Least privilege.** `package.json` asks for `health` and `configurable`
  only. There is no location, and no network from the watch.
- **Claims.** Every privacy statement on the page and in the README matches
  the code. The store listing does not (SEC-1).

### Stability and robustness

- **The whole night through the real worker** (`tests/worker_sim`). It covers
  four restful episodes, one of them ending inside the window, one Measure now
  left to finish and one cancelled after 30 seconds:
  - five measurements are stored, one flagged manual; the cancelled one
    stores nothing;
  - every stored measurement also goes to data logging;
  - the sensor is held for exactly 4 × 120 + 60 + 30 seconds and released
    afterwards.
- **The sensor is released on every exit path.** By reading the code: the end
  of the window, the end of the episode, switching off, cancel, and
  `prv_deinit()` when the worker is stopped. `prv_init()` also releases it, in
  case a previous run was killed without deinitialising.
- **Worker memory.** 4,372 of 10,240 bytes, from the build report. The only
  large buffer is static rather than on the stack. The worker binary contains
  no floating-point routines, only the 64-bit integer division helper, checked
  with `arm-none-eabi-nm`.
- **Persistent storage.**
  - Every write is at most 256 bytes: the history is exactly 256, the
    diagnostics 36. The simulator fails any write over 256.
  - Reads check their length, and a diagnostics struct of the wrong size is
    reset rather than misread.
  - The old six-byte history keys are deleted by both the app and the worker.
- **The watch app.**
  - Every timer is cancelled when its window unloads.
  - Measure now checks that its layer exists before redrawing.
  - The phone message fits its outbox: 307 of 388 bytes at most.
  - The one-retry rule stops a failed send from looping.
- **Time.**
  - Nights on the page run noon to noon in local time, including across both
    DST changes, month ends and year ends (`tests/page_stats.test.js`).
  - The worker's window follows UTC (see STAB-4 for clock corrections).

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
  rules, the two middle measurements behind an even median, and Measure now
  readings never reaching a night.
- **Link decoding** (`tests/decode.test.js`):
  - the manual flag and the artefact count stay separate;
  - timestamps after 2038 stay positive;
  - old six-byte records have no count;
  - a trailing partial record is ignored;
  - junk characters in the base64 are skipped;
  - the CSV has one row per measurement.

### Battery and performance

- **Sensor time.** The sensor runs at one reading a second only during a
  measurement: 120 seconds per restful episode, or per Measure now. That is 8
  to 10 minutes on a typical night of 4 to 5 episodes. The rest of the time
  the worker asks for no sample period at all.
- **Wake-ups.** The worker ticks once a minute when idle and once a second only
  inside a measurement. The system's HRV broadcast, about 2,000 a day, wakes it
  briefly to increment a counter.
- **Flash.**
  - After BATT-1, a night takes about 60 writes: diagnostics at sleep changes,
    at the start and end of measurements, and every 15 minutes, plus the
    history.
  - Measure now adds 120 (BATT-2).
  - Flash reads are cheap by comparison: the on/off setting is read once a
    second during a measurement.
- **Phone.** The app sends one message of at most 307 bytes each time it is
  opened. The settings link is at most about 6 kB.

## Limitations

- **No hardware.** No watch was available and the emulator does not run in the
  review environment. Two things therefore need a hardware check before
  release:
  - Measure now as a whole: the countdown, the result, cancel, and the "off"
    and "busy" screens;
  - that the flash fix makes no difference to how the worker behaves over a
    real night.
- **The simulator is a model.** It stands in for the SDK, and its night is
  invented. Real health events arrive on the firmware's own schedule, so the
  exact counts will differ.
- **The Pebble mobile app is outside this review.** Its handling of AppMessage,
  `localStorage` and data logging could not be checked (SEC-1, SEC-2).

## Rerunning the checks

```sh
apps/restful-hrv/tests/run.sh
```

Needs `gcc`, `python3` 3.9+ and `node`. The fuzz test also needs Playwright,
and is skipped otherwise. See [`tests/README.md`](tests/README.md) for what each
check does. No real measurements are kept in the repository: all test data is
synthetic.
