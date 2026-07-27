# Rebble App Store listing — One-Off Alarm

Copy-paste these values when you run `pebble publish` in `apps/one-off-alarm`.

## App name

```
One-Off Alarm
```

## Version

```
1.1
```

## Category

```
tools
```

## Short description

```
Set a single wake-up alarm for any future date - not just a recurring daily one.
```

## Full description

```
One-Off Alarm lets you schedule a single wake-up alarm for any point in the
future - "in 3 weeks at 9 AM", "tomorrow at 6:30" - instead of a recurring
daily alarm like the built-in Alarms app.

Pick how many days from now, the hour, and the minute using UP/DOWN, cycle
between fields with SELECT, and hold SELECT to confirm. Choose how the alarm
vibrates - Mild, Medium, or Aggressive - and feel each pattern instantly as
you pick it.

Once set, the watch will wake you up at the exact moment you chose, even if
you've closed the app in the meantime - no phone required at the time it
fires.
```

## Release notes (v1.0)

```
Initial release.

- Set a one-off wake-up alarm for any future day and time (not a
  recurring daily alarm).
- Choose Mild, Medium, or Aggressive vibration when the alarm fires -
  each pattern plays instantly as you pick it, so you know what to
  expect.

Reliability - what makes sure it actually wakes you up:
- Uses the watch's built-in Wakeup system, so the alarm is scheduled
  at the OS level - it still fires even if you close the app or the
  watch reboots in the meantime.
- Handles both ways the watch can deliver the alarm: a fresh launch
  when the app is closed, and a direct signal when the app happens to
  already be open (e.g. left on the countdown screen). Earlier builds
  only handled the first case, which meant the alarm could silently
  fail to go off in that second scenario - this is now fixed.
- Requires at least 30 seconds of lead time when scheduling, per a
  watch OS limit, and warns you on-screen if you pick a time too close
  to "now" instead of silently failing.
```

## Release notes (v1.1)

```
- Cancelling a set alarm now requires holding SELECT (same as
  confirming a new one), instead of a single short press. A short
  press previously cancelled it outright, which was too easy to
  trigger by accident.
- The app menu now shows a live countdown under the app's name while
  an alarm is set (e.g. "1d 4h left") - only while one is actually
  scheduled, and it clears itself automatically the moment the alarm
  fires.
```

## Source URL

```
https://github.com/MrMastodon/Pebble-Watch-apps/tree/main/apps/one-off-alarm
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
cd apps/one-off-alarm
pebble login
pebble publish \
  --name "One-Off Alarm" \
  --version 1.1 \
  --description "Set a single wake-up alarm for any future date - not just a recurring daily one." \
  --category tools \
  --release-notes "Cancelling now requires holding SELECT instead of a single short press, so it can't happen by accident." \
  --icon-small appstore/icon-small.png \
  --icon-large appstore/icon-large.png
```

Running it without the flags works too — `pebble publish` will prompt
interactively for each of these values (and for screenshots, which it can
also capture automatically from the emulator with `--gif-all-platforms`).
