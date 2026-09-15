# Restful HRV

A watchapp for the Pebble Time 2 that measures heart rate variability (HRV)
automatically while you sleep, and logs each result to your phone. It takes one
measurement every time the watch enters restful sleep, so you end up with a
number per deep-sleep episode rather than one per night.

The measurement is deliberately identical every time — same duration, same
sampling rate, always taken at the start of an episode — because an HRV reading
is only useful when it can be compared against the ones before it.

The app itself does almost nothing: it is a single on/off switch. All the work
happens in a background worker that keeps running with the app closed.

## Using it

```
      HRV Measuring
  ──────────────────────

            ON

    Background: running

   Measures HRV during
   restful sleep.
   SELECT to turn off.

           v1.0
```

- **SELECT** — turn measuring on or off. The setting is remembered across
  reboots and reinstalls.
- **Background** — whether the worker is actually running. Turning measuring on
  launches it; turning measuring off stops it, so it isn't holding the watch's
  single background-app slot for nothing.

There is no history screen. Results go straight to the phone via DataLogging,
and a second copy on the watch would only be a second thing that can disagree
with the first.

The first time you turn it on, the watch may ask whether this app's background
worker may replace whichever one is currently installed — Pebble allows only one
at a time. Until you accept, the screen shows `Background: starting...`.

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
| Minimum usable readings | 10 peak-to-peak intervals |
| Measurements per night | one per restful sleep episode |

A measurement that collects fewer than 10 intervals is **discarded, not logged**
— usually a wrist position the sensor couldn't read through. A bad night shows
up as a gap in the data rather than as a plausible-looking wrong number.

If restful sleep ends before the 120 seconds are up, the measurement stops
immediately and whatever was collected is logged, as long as it cleared that
floor of 10.

## Getting the data off the watch

Each completed measurement is one record of two 4-byte little-endian unsigned
integers, appended to a DataLogging session:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | UTC timestamp when the measurement ended |
| 4 | 4 | RMSSD in whole milliseconds |

The session is tagged `0x48525631` (ASCII `HRV1`), with `DATA_LOGGING_UINT` and
an item length of 4. Records are sent to the phone whenever it's in range, and
buffered on the watch until then.

To read them with the SDK tool:

```sh
pebble data-logging list --phone <ip>
pebble data-logging download hrv.bin --session-id <id> --phone <ip>
```

Then decode pairs of `uint32` little-endian values, e.g.:

```python
import struct, datetime
data = open("hrv.bin", "rb").read()
for ts, rmssd in struct.iter_unpack("<II", data):
    print(datetime.datetime.fromtimestamp(ts), rmssd, "ms")
```

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

Two binaries:

- `worker_src/c/worker.c` — the background worker. Subscribes to
  `HealthService` events and, as a fallback, polls
  `health_service_peek_current_activities()` on a tick, because health events
  fire on the health service's own schedule and an episode could otherwise start
  or end minutes before the worker noticed. On the transition into restful sleep
  it requests an HRV sample period, collects
  `health_service_peek_hrv_ppi_ms()` readings for 120 seconds, computes RMSSD,
  logs it, and releases the sample period.
- `src/c/main.c` — the switch. Writes the setting to persistent storage and
  launches or kills the worker to match.

The two share `src/common/hrv_common.h` so the persistent-storage key can't
drift between them — if it did, the switch would silently stop reaching the
worker.

The sample period is released on every path out of a measurement, including
worker shutdown, so the sensor is never left running at the elevated rate.

## Building

```sh
cd apps/restful-hrv
pebble build
pebble install --emulator emery     # or --phone <ip>
```

Note that the emulator can't reproduce the real trigger: `pebble emu-sleep` sets
the sleep *metrics* but not the `HealthActivityRestfulSleep` activity bit, and
there is no way to inject peak-to-peak intervals. Emulator testing of the
measurement path needs a scratch build with those two sensor reads stubbed out.
