# Rebble App Store listing — Restful HRV

Copy-paste these values when you run `pebble publish` in `apps/restful-hrv`.

## App name

```
Restful HRV
```

## Version

```
1.9.0
```

## Category

```
health and fitness
```

## Short description

```
Measures your HRV automatically every time you drop into restful sleep.
```

## Full description

```
Restful HRV measures heart rate variability while you sleep, without you
having to remember anything. Every time the watch detects restful sleep, it
takes a two-minute reading and works out your RMSSD in milliseconds - the
standard short-window HRV metric.

Every measurement is taken exactly the same way: the same two-minute window,
the same one-second sampling, always at the start of a restful sleep episode.
That is the whole point - an HRV number is only worth anything if you can
compare it against the ones from previous nights. Beats the sensor misread
are filtered out, and a reading without enough clean beats is dropped rather
than logged, so a bad night shows up as a gap instead of a wrong number.

On the watch:
- SELECT turns measuring on and off.
- DOWN shows your measurements, newest first. The watch keeps about a month
  of them.
- UP is Measure now: a two-minute reading on demand, with a countdown ring.
  Readings taken this way are marked as manual and kept out of the nightly
  figures, since you are awake.

On the phone, open the app's settings. Your measurements are summed up night
by night - each night is the median of its readings - with your latest night
against the nights before it, 7- and 30-night averages, a normal range, how
steady your nights are, and a chart. Tap a night to see its readings. Copy or
download everything as CSV. The figures only ever compare you with yourself;
this is not a medical assessment.

Privacy: this app sends your measurements to no server. The settings page is
a static file hosted on GitHub; your measurements travel to it inside the
link itself, in the part after the #, which browsers never send to any server.
GitHub sees only that the page was opened. On your phone the measurements
pass through the Pebble app, which holds the app's cache and shows the page.
Anyone you give the link to can see the measurements in it.

The code has been reviewed for security, privacy, stability, measurement
correctness and battery use; the report is published with the source.

Requires a Pebble Time 2 with sleep tracking on - HRV beat-to-beat intervals
need firmware 4.32 or newer and a heart rate sensor that reports them.
```

## Release notes (v1.9.0)

```
- Nights, not single readings: the phone shows each night as the median of
  its readings, compared with your own recent nights - 7- and 30-night
  averages, a normal range, and how steady your nights are - with a chart
  and one row per night that opens to show its readings.
- Measure now (UP): a two-minute reading on demand with a countdown.
  Kept separate from the nightly figures.
- The watch keeps about a month of measurements (up to 128, was 32), so
  going a while without opening the app no longer loses the oldest ones.
- Beats the sensor misread are filtered out before RMSSD is computed.
- Measurements are no longer sent to Pebble's data logging, which nothing
  read.
- Far fewer flash writes overnight, and several robustness fixes from a
  code review. The report is published with the source.
- New icon that shows in the app list, not only when selected.
```

## Earlier release notes

Kept as they were published; details have changed since (see v1.9.0).

### v1.1

```
- The watch now keeps your last 40 measurements and lists them in the app,
  newest first - press DOWN from the on/off screen.
- Added a settings page on the phone showing the same measurements as a
  table and a chart, with copy and download as CSV. It is a static page
  with no server behind it: the numbers travel in the URL fragment, which
  browsers never send to a host, so nothing is uploaded anywhere.
- Measurements reach the phone when you open the app on the watch. Pebble
  background workers cannot talk to the phone at all, so there is no way
  around that; the settings page says how recently it was refreshed.
```

### v1.0

```
Initial release.

- Measures HRV (RMSSD) automatically for two minutes at the start of every
  restful sleep episode, using the watch's own sleep detection.
- Every measurement uses identical settings - fixed duration, fixed sample
  period - so nightly numbers can actually be compared with each other.
- Several restful sleep episodes in one night produce several separate
  readings rather than a single nightly average.
- Measurements that collect too few clean readings are discarded instead of
  logged, so poor sensor contact leaves a gap rather than a misleading value.
- A measurement cut short by waking up is still logged, if it gathered
  enough to be worth keeping.
- Results are logged to the phone with a timestamp, under data logging tag
  HRV1.
- One screen, one button: SELECT turns measuring on and off, and the
  background worker is started and stopped to match, so it isn't holding the
  watch's single background-app slot while switched off.
- The heart rate sensor is only driven at the elevated rate during those two
  minutes, and the sample period is released on every exit path - including
  the worker being shut down.
```

## Source URL

```
https://github.com/MrMastodon/Pebble-Watch-apps/tree/main/apps/restful-hrv
```

## Icons

- `icon-small.png` — 48×48, for the `--icon-small` flag / iconSmall upload field.
- `icon-large.png` — 144×144, for the `--icon-large` flag / iconLarge upload field.

## Banner

- `banner.png` — 720×320, shown above the screenshots on the store page.
  Required for apps (optional for watchfaces). `pebble publish` has no
  `--banner` flag, so upload this one through the web dashboard
  (CloudPebble/Rebble developer portal) after the app is created, or if
  the interactive `pebble publish` prompt asks for it directly.

## Example command

```sh
cd apps/restful-hrv
pebble login
pebble publish \
  --name "Restful HRV" \
  --version 1.9.0 \
  --description "Measures your HRV automatically every time you drop into restful sleep." \
  --category "health and fitness" \
  --release-notes "Nightly summaries against your own recent nights, Measure now, a month of history on the watch, and fixes from a code review." \
  --icon-small appstore/icon-small.png \
  --icon-large appstore/icon-large.png
```

Running it without the flags works too — `pebble publish` will prompt
interactively for each of these values (and for screenshots, which it can
also capture automatically from the emulator with `--gif-all-platforms`).
