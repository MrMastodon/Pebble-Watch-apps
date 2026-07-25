# Pebble-Watch-apps

A collection of small, standalone watchapps for the Pebble Time 2 ("emery"
platform), built with Core Devices' Pebble SDK.

Each app lives in its own folder under `apps/`, buildable independently with
the `pebble` CLI:

```sh
cd apps/<app-name>
pebble build
pebble install --emulator emery
```

## Apps

- [`apps/one-off-alarm`](apps/one-off-alarm) — set a single wake-up alarm for
  an arbitrary future date/time (e.g. "in 3 weeks at 9 AM"), instead of a
  recurring daily alarm.
