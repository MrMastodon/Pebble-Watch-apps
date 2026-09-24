# Restful HRV

A watchapp for the Pebble Time 2 that measures heart rate variability (HRV)
automatically while you sleep, and logs each result to your phone. It takes one
measurement every time the watch enters restful sleep, so you end up with a
number per deep-sleep episode rather than one per night.

The measurement is deliberately identical every time — same duration, same
sampling rate, always taken at the start of an episode — because an HRV reading
is only useful when it can be compared against the ones before it.

The app itself is a switch and a list. All the measuring happens in a background
worker that keeps running with the app closed.

## Using it

```
      HRV Measuring
  ──────────────────────

            ON

    Background: running

   SELECT to turn off
   DOWN for history

           v1.1
```

- **SELECT** — turn measuring on or off. The setting is remembered across
  reboots and reinstalls.
- **DOWN** — the measurement history, newest first.
- **UP** — what the worker saw overnight, for when nothing was measured. Holding
  SELECT there runs a measurement immediately, without waiting for sleep.
- **Background** — whether the worker is actually running. Turning measuring on
  launches it; turning measuring off stops it, so it isn't holding the watch's
  single background-app slot for nothing.

The history screen lists what has been measured, newest at the top:

```
   Last 15 measurements
  ──────────────────────
   61 ms
   Tue 15 Sep, 03:12
  ──────────────────────
   54 ms
   Tue 15 Sep, 01:40
  ──────────────────────
```

The watch keeps the last 32 measurements — about a week and a half at three or
four restful sleep episodes a night. A value that needed cleaning shows how many
intervals were removed, e.g. `29 ms (-2)`. Times follow the watch's own 12/24-hour setting.

The first time you turn it on, the watch may ask whether this app's background
worker may replace whichever one is currently installed — Pebble allows only one
at a time. Until you accept, the screen shows `Background: starting...`.

## The watch's copy is not the durable one

Removing a watchapp deletes its persistent storage outright. From the firmware's
`app_install_manager.c`:

```c
case APP_UPGRADED:
  app_upgrade = true;
  /* fallthrough */
case APP_REMOVED:
  // Only delete the app's persist file when the user explicitly removes the
  // app, not during an AppDB clear.
  if (!app_upgrade) {
    persist_service_delete_file(s_install_callback_data.uuid);
```

An in-place upgrade keeps the data and a plain resync keeps it, but a removal
does not - and the phone is what decides to remove an app. A sideloaded copy
that is not in your locker can be removed on the next sync, taking the history,
the diagnostics and the on/off setting with it. The giveaway is the switch
reading `ON` again: that is the default when the key does not exist.

This is why the app pushes everything to the phone whenever you open it, and why
PebbleKit JS **merges** rather than replaces what it holds. The watch keeps the
last 32 measurements and can lose them at any moment; the phone keeps up to 500
and is what survives. An incoming batch is treated as new information about the
past, never as the whole of it - replacing would discard everything older than
whatever the watch happened to be holding at the time.

The way to stop the removals is to have the app in your locker rather than
sideloaded.

## When nothing gets measured

A night that produces no measurements is otherwise completely silent about why,
since `APP_LOG` only exists while a computer is tethered. The **UP** button
shows what the worker actually saw:

```
   WORKER
   Started: Wed 23:14
   Last alive: Thu 07:02

   SLEEP DETECTION
   Asleep: Thu 01:20
   Restful: never
   Sleep events: 6

   SENSOR
   HRV readings: 0
   Period granted: not requested

   MEASUREMENTS
   Episodes: 0
   Last result: none yet
```

Each line separates one failure from another:

| What you see | What it means |
|---|---|
| `Last alive` hours old, or `never` | The worker was not running. Check the switch screen says `Background: running`. |
| `Asleep: never` | The watch never registered you as asleep, so nothing downstream could fire. |
| `Asleep` set, `Restful: never` | Sleep was tracked but never classified as restful. Nothing is wrong with the app; the trigger simply never occurred. |
| `Episodes` above zero, `While measuring: 0` | Measurements ran but no HRV reading arrived inside any of their windows. |
| `While measuring` high, `Of those, empty` about the same | Readings arrived and every one came back without an interval. |
| `Last result: too few readings` | The sensor delivered some intervals, but fewer than the ten needed. |
| `Last result: no intervals`, `Empty` counting up | The sensor was running and every reading came back without a heartbeat interval. See below. |
| `Last result: died computing` | The worker did not survive turning the readings into a number. It should never say this; if it does, that is a bug worth reporting. |

### "HRV readings" all day is not the same as readings you can use

`HealthEventHRVUpdate` is broadcast to every health service subscriber
unconditionally. The per-subscriber feature filter in `hrm_manager.c` applies to
the raw HRM stream, not to this:

```c
if (data->features & HRMFeature_HRV) {
  PebbleEvent health_event = { ... .ppi_ms = data->hrv_ppi_ms ... };
  event_put(&health_event);
}
```

So this app receives HRV events whenever the sensor produces any, whoever asked
for them, as long as something holds an HRM subscription at all - which Pebble
Health does, all day, for the heart rate graph. A large "HRV readings" count
therefore says nothing about whether a measurement had anything to work with.
Only the count taken inside a measurement window does, which is why the two are
reported separately.

### A peak-to-peak interval of zero is not "no reading yet"

The SDK documents `health_service_peek_hrv_ppi_ms()` as returning "0 if no
reading is available yet", which hides what a zero actually is. In the Pebble
Time 2's sensor driver (`gh3x2x.c` in PebbleOS) a real reading skips any
interval that is not plausible:

```c
if ((rri[i] <= 0) || (rri[i] > UINT16_MAX)) {
  continue;
}
```

and a zero is emitted from exactly one place - the branch taken when the watch
does not believe it is being worn:

```c
if (!HRM->state->is_wear) {
  HRMData hrm_data = {0};              // hrv_ppi_ms = 0
  hrm_data.features = HRMFeature_HRV;
  hrm_data.hrv_quality = HRMQuality_OffWrist;
  hrm_manager_new_data_cb(&hrm_data);
  return;
}
```

So a measurement full of zeroes is not a weak signal - it is the watch not
believing it is being worn, while still reporting thousands of "HRV readings",
which makes the sensor look like it is working perfectly.

That is **not** the same as the watch being off your wrist. The heart rate path
in the same driver gates on the same flag:

```c
if (!HRM->state->is_wear) {
  hrm_data.hrm_quality = HRMQuality_OffWrist;
} else {
  hrm_data.hrm_bpm = bpm;
```

so a night that produced a heart rate graph is a night when the flag was set at
least some of the time. `HealthMetricHeartRateBPM` is averaged over whole
minutes, so a flag that flickers can still leave a continuous-looking graph
while costing every HRV reading, which needs the flag set at that instant. The
app counts empty readings separately and reports `no intervals` rather than
`too few readings`, without claiming to know which of the two it is.

### Artefact filtering

A single beat the sensor misses doubles one interval, and the two huge
successive differences around it dominate the entire sum of squares. In a
simulated calm window of about 30 ms:

| Window | RMSSD |
|---|---|
| clean | 29 ms |
| one missed beat | 135 ms |
| two missed beats | 188 ms |
| one false extra peak | 73 ms |

Unfiltered, that is where the occasional 120-180 ms value in an otherwise
25-50 ms series comes from. So before RMSSD is computed, any interval that
differs by more than 20 % from the previous *accepted* interval is rejected (the
Malik criterion), and successive differences are taken between the intervals
that remain. In the same simulation that returns 29 ms in all three failure
cases, and leaves the clean window untouched - beat-to-beat changes in restful
sleep are nowhere near 20 %.

Two details matter:

- The comparison is against the last accepted interval, not simply the previous
  one. Otherwise the normal interval after a doubled one would be rejected too,
  for differing from the error.
- The reference starts at the first interval that agrees with its successor, so
  a window that happens to open on an artefact does not measure everything
  against it and discard the whole window.

The number of rejected intervals is kept with every measurement, shown in the
history and on the settings page, and included in the CSV and in DataLogging.
The ten-interval floor applies after filtering: ten intervals of which eight
were artefacts is not a measurement.

Measurements taken before filtering was added are not comparable with those
taken after, so the upgrade starts a new series: the watch's old history is
removed, and the phone keeps its old cache untouched but no longer shows it.

### The buffer must not be the binding constraint

The interval buffer holds 300 readings, not one per second of the window. Real
hardware delivered around 136 intervals in a 120-second window at rest, so a
buffer of 120 filled and the remainder were discarded - which quietly turns the
measurement from "120 seconds" into "the first N beats". That varies in duration
with heart rate, which is exactly the comparability the app is built to protect.

### No floating point in the worker

RMSSD is computed with 64-bit integers and an integer square root, not
`sqrt()` on doubles. A background worker gets a much smaller stack than an app,
and the emulator is more forgiving about that than the watch is - which is
exactly the shape of bug that survives every test you run and then fails only on
real hardware, silently, in the middle of the night.

The arithmetic never needed a fraction: the inputs are whole milliseconds and so
is the answer. The integer version agrees exactly with the floating-point one
across several hundred generated cases, including the range extremes.

For the same reason, the measurement's outcome is written to storage *before*
the result is computed rather than after. If the worker dies doing the
arithmetic, the status screen says `died computing` instead of looking
indistinguishable from a measurement that simply never finished.

### Testing without waiting for a night

Holding SELECT on the status screen asks the worker to run a measurement now.
It is the same code on the same sensor subscription as a real one - the only
difference is that it is not cancelled by you being awake - and the counters
above it update every second while it runs.

This exists because debugging against real sleep costs a night per attempt,
which is far too slow to find anything out. Sit still for two minutes with the
watch on and watch `While measuring` and `Of those, empty` move.

A manual measurement is logged and appears in the history like any other, so
expect test values among your real ones.

The counters are cumulative and survive reboots. They are reset by removing the
app - see above - and they are mirrored to the phone's settings page, which is
where to look if the watch's copy has been wiped.

## What it measures

**RMSSD** — the root mean square of successive differences between heartbeats,
reported in whole milliseconds. It is the standard short-window HRV metric, and
the one most comparable across nights.

Fixed parameters, the same for every measurement:

| | |
|---|---|
| Trigger | entering `HealthActivityRestfulSleep` |
| Duration | 120 seconds |
| Sample period | 1 second (the shortest the SDK accepts) |
| Artefact filter | an interval is dropped if it differs from the previous accepted one by more than 20 % |
| Minimum usable readings | 10 peak-to-peak intervals, counted after filtering |
| Interval buffer | 300, so the window stays the constraint, not the buffer |
| Measurements per night | one per restful sleep episode |

Verified on hardware: a night produced four restful sleep episodes and four
recorded measurements, using 116 intervals for the last of them.

A measurement that collects fewer than 10 intervals is **discarded, not logged**
— usually a wrist position the sensor couldn't read through. A bad night shows
up as a gap in the data rather than as a plausible-looking wrong number.

If restful sleep ends before the 120 seconds are up, the measurement stops
immediately and whatever was collected is logged, as long as it cleared that
floor of 10.

## Seeing it on the phone

Open the app's settings from the Pebble mobile app (the gear icon next to
Restful HRV) and you get the stored measurements summed up night by night, with
buttons to copy or download them as CSV.

**The page compares nights, not measurements.** A night can hold anything from
one measurement to ten, so an average over measurements lets long nights count
several times over, and a single bad episode can drag a whole night with it.
Instead every night gets one value, the **median** of its measurements, and all
the figures are built from those:

| Figure | What it is | Shown from |
|---|---|---|
| Latest night | That night's median, and how far it is from the average of the up to 7 nights before it. The night itself is left out of that average, so it is never compared with itself. | 3 earlier nights |
| 7 nights | Average of the last 7 nights with measurements | 3 nights |
| 30 nights | Average of the last 30 nights, and the normal range: that average ± one standard deviation | 3 nights; range from 10 |
| Stability | Coefficient of variation over the last 7 nights (standard deviation ÷ average). Lower means steadier nights. | 5 nights |

Below those minimums the page says how many nights it has so far instead of
showing a number. A standard deviation of three nights moves so much from one
night to the next that it describes the sample, not you. Where a figure covers
fewer nights than its window, it says how many.

A night runs noon to noon in the phone's local time and is named after the
morning it ends. So a measurement at 23:53 on Monday belongs to Tuesday's night.
The chart places nights by date, so a night without a measurement shows up as a
gap. It draws the 7-night average through the nights, and the normal range
across the last 30 nights. The list under it has one row per night, and you
tap a row to see that night's measurements. The CSV stays one row per
measurement.

These numbers only compare you with yourself. HRV differs a great deal from
person to person, and none of this is a medical assessment.

A few other things are worth knowing about how the page works:

**The measurements only reach the phone while the watchapp is open.** Pebble
background workers have no AppMessage, so the worker cannot talk to the phone at
all. The watchapp pushes its whole history when you open it, and the settings
page shows the most recent push — it tells you how long ago that was. Open the
app on the watch to refresh it.

**Downloading only works in a real browser.** The Pebble app's settings window
cannot save files - on Android its WebView has no `DownloadListener`, so every
download is silently dropped, and every link loads inside the same window. So
inside the app the page offers *Copy as CSV* and *Copy link for browser* instead
of a download button. The link carries the measurements in its fragment, so
pasted into the browser's address bar it opens the same page, where *Download
CSV* works. The bare address without the fragment is an empty page, since there
is nothing on the server to load. Deleting only works from inside the app.

The link is long, because the measurements are inside it: a little under 1,000
characters for a week, and up to about 6,000 with the 500 measurements the
phone keeps. The page says how long it is right now. Paste it straight into the
address bar. Chat and notes apps can cut a long link short or break it across
lines, and a cut link opens the page with measurements missing, without any
error.

**Nothing is uploaded anywhere.** The settings page is a static file with no
backend, and the measurements travel to it in the URL fragment, which browsers
never send to the server. The page makes no network requests of its own. Its
source is in [`docs/restful-hrv/`](../../docs/restful-hrv) in this repository.

## Getting the raw data

Alongside the on-watch history, each completed measurement is appended to a
DataLogging session as one record of three 4-byte little-endian unsigned
integers:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | UTC timestamp when the measurement ended |
| 4 | 4 | RMSSD in whole milliseconds, after artefact filtering |
| 8 | 4 | intervals rejected as artefacts |

The session is tagged `0x48525632` (ASCII `HRV2`), with `DATA_LOGGING_UINT` and
an item length of 4. Earlier builds logged two-item records without filtering
under `HRV1`; the tag changed so the two computations cannot be mistaken for
one another. Unlike the on-watch history, this is not capped.

DataLogging data **cannot be read by PebbleKit JS** — only by a native companion
app built with PebbleKit Android or iOS, or over the Developer Connection:

```sh
pebble data-logging list --phone <ip>
pebble data-logging download hrv.bin --session-id <id> --phone <ip>
```

```python
import struct, datetime
data = open("hrv.bin", "rb").read()
for ts, rmssd, rejected in struct.iter_unpack("<III", data):
    print(datetime.datetime.fromtimestamp(ts), rmssd, "ms", rejected, "rejected")
```

For everyday use the settings page above is the easier route; this one exists
for anyone who wants every measurement ever taken rather than the last 32.

## Hosting the settings page

The page is served from `docs/restful-hrv/` via GitHub Pages, which has to be
enabled once for this repository (Settings → Pages → Source: `main` branch,
`/docs` folder). The URL is set in `CONFIG_URL` at the top of
`src/pkjs/index.js`; change it there if the page moves.

Being a static file with no backend, it can be hosted anywhere, including
opened straight from disk for development.

## Deleting the measurements

The settings page has a *Delete all measurements* button, behind a second tap.

It has to go via the watch. The phone holds the durable copy, but the watch
holds its own last 32, so clearing only the phone would achieve nothing - the
next time you open the app on the watch it sends them straight back and the
merge restores them. And the watch can only be told while the app is open on it,
since a background worker has no AppMessage.

So the button records the request, and:

- if the watchapp is open, the request goes across immediately;
- otherwise the page shows *Deletion pending* until you next open it, and
  anything the watch sends in the meantime is ignored rather than merged back;
- the phone drops its copy only once the watch confirms it has forgotten, so a
  half-finished clear leaves both sides holding the data rather than one of them
  losing it.

The diagnostics counters are kept - they describe the worker's behaviour, not
your data. Records already handed to DataLogging are not affected either;
nothing on the page can reach those.

## Requirements

- **Pebble Time 2.** HRV peak-to-peak intervals need firmware 4.32 or newer and
  a heart rate sensor that reports them; at the time of writing that is the
  Pebble Time 2 only. On other watches the app runs but never collects enough
  readings to log anything.
- Sleep tracking enabled, since the trigger is the watch's own restful sleep
  detection. The app does no sleep staging of its own.

## Battery

The heart rate sensor is only driven at the measurement rate for those 120
seconds per episode; the rest of the time the app releases its HRV sample
period entirely and the system goes back to its own schedule. While measuring is
switched off, the worker isn't running at all.

## How it works

Three pieces:

- `worker_src/c/worker.c` — the background worker. Subscribes to
  `HealthService` events and, as a fallback, polls
  `health_service_peek_current_activities()` on a tick, because health events
  fire on the health service's own schedule and an episode could otherwise start
  or end minutes before the worker noticed. On the transition into restful sleep
  it requests an HRV sample period, collects
  `health_service_peek_hrv_ppi_ms()` readings for 120 seconds, computes RMSSD,
  writes it to both the on-watch history and DataLogging, and releases the
  sample period.
- `src/c/main.c` — the switch and the history list. Writes the setting to
  persistent storage, launches or kills the worker to match, and hands the
  history to PebbleKit JS.
- `src/pkjs/index.js` + `docs/restful-hrv/index.html` — the phone side. The JS
  caches whatever the watch sends and passes it to the settings page in the URL
  fragment.

`src/c/main.c` and `worker_src/c/worker.c` share `src/common/hrv_common.h` so
the persistent-storage keys and the record layout can't drift between them — if
they did, the switch would silently stop reaching the worker and the history
would decode as nonsense.

The history is written before the DataLogging call, so a full DataLogging
session never costs you the number itself. The HRV sample period is released on
every path out of a measurement, including worker shutdown, so the sensor is
never left running at the elevated rate.

The watch sends its history on two independent triggers: when PebbleKit JS
announces itself, and from a timer a couple of seconds after launch. Either one
alone would usually work; both together mean a settings page that is silently
always empty needs two things to fail rather than one.

## Building

```sh
cd apps/restful-hrv
pebble build
pebble install --emulator emery     # or --phone <ip>
```

`"enableMultiJS": true` in `package.json` is required, not optional. Without it
the SDK bundles `src/pkjs/index.js` into the `.pbw` under its own name, while
the phone looks for `pebble-js-app.js` at the bundle root and silently runs no
JavaScript at all when it is not there. Everything on the watch keeps working,
so the only symptom is that the settings page never opens and nothing is ever
logged from the phone side. The build says so, quietly:

```
WARNING: enableMultiJS is not enabled for this project and pebble-js-app.js does not exist
```

To check a build, list the bundle - `pebble-js-app.js` has to be in it:

```sh
unzip -l build/restful-hrv.pbw
```

Note what the emulator cannot reproduce:

- **The trigger.** `pebble emu-sleep` sets the sleep *metrics* but not the
  `HealthActivityRestfulSleep` activity bit, and there is no way to inject
  peak-to-peak intervals. Testing the measurement path there needs a scratch
  build with those two sensor reads stubbed out.
- **PebbleKit JS.** The emulator's phone simulator acknowledges AppMessages but
  does not appear to run the app's JavaScript or surface its `console.log`, so
  the settings page can only be exercised for real against a phone. The page
  itself can be opened directly in a browser with a hand-made fragment.

## When the settings page will not open

Tapping the gear icon starts `src/pkjs/index.js` and then waits for it to call
`Pebble.openURL()`. If that call is never reached the phone sits on *Loading
watch app* indefinitely, with nothing on screen to say why. The
`showConfiguration` handler is therefore registered before anything else that
could throw, and always opens the page even if building the URL fails - an
empty page beats a spinner that never resolves.

To see how far it gets, watch the JavaScript console on a real phone:

```sh
pebble logs --phone <ip>
```

The script announces each step, prefixed `Restful HRV:`. Seeing `script loaded`
but never `opening settings with N measurements` means the handler is not being
called at all, rather than failing inside.

To rule out the page and the network entirely, open the configuration URL
directly in the phone's browser. If that works, the problem is on the Pebble
side; if it does not, the page is not reachable from that phone.
