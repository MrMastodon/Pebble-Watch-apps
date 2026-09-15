# Rebble App Store listing — Restful HRV

Copy-paste these values when you run `pebble publish` in `apps/restful-hrv`.

## App name

```
Restful HRV
```

## Version

```
1.0
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
the same one-second sampling, always at the start of a deep-sleep episode.
That is the whole point - an HRV number is only worth anything if you can
compare it against the ones from previous nights.

Several restful sleep episodes in a night give you several readings, not one
average. Readings the sensor couldn't get a clean look at are thrown away
rather than logged, so a bad night shows up as a gap instead of a wrong
number.

The app is a single on/off switch. Results are sent straight to your phone
through Pebble's data logging, tagged HRV1, as a timestamp and an RMSSD value
per measurement.

Requires a Pebble Time 2 - HRV peak-to-peak intervals need firmware 4.32 or
newer and a heart rate sensor that reports them.
```

## Release notes (v1.0)

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
  --version 1.0 \
  --description "Measures your HRV automatically every time you drop into restful sleep." \
  --category "health and fitness" \
  --release-notes "Initial release." \
  --icon-small appstore/icon-small.png \
  --icon-large appstore/icon-large.png
```

Running it without the flags works too — `pebble publish` will prompt
interactively for each of these values (and for screenshots, which it can
also capture automatically from the emulator with `--gif-all-platforms`).
