# Rebble App Store listing — One-Off Alarm

Copy-paste these values when you run `pebble publish` in `apps/one-off-alarm`.

## App name

```
One-Off Alarm
```

## Version

```
1.0
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

## Source URL

```
https://github.com/MrMastodon/Pebble-Watch-apps/tree/main/apps/one-off-alarm
```

## Icons

- `icon-small.png` — 80×80, for the `--icon-small` flag / iconSmall upload field.
- `icon-large.png` — 144×144, for the `--icon-large` flag / iconLarge upload field.

## Example command

```sh
cd apps/one-off-alarm
pebble login
pebble publish \
  --name "One-Off Alarm" \
  --version 1.0 \
  --description "Set a single wake-up alarm for any future date - not just a recurring daily one." \
  --category tools \
  --icon-small ../../appstore/one-off-alarm/icon-small.png \
  --icon-large ../../appstore/one-off-alarm/icon-large.png
```

Running it without the flags works too — `pebble publish` will prompt
interactively for each of these values (and for screenshots, which it can
also capture automatically from the emulator with `--gif-all-platforms`).
