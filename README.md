# Pebble-Watch-apps

A collection of small, standalone watchapps for the Pebble Time 2 ("emery"
platform), built with [Core Devices'](https://developer.repebble.com/) Pebble
SDK. Each app solves one narrow, self-contained problem — no companion app,
no shared code between them.

## Project layout

Every app lives in its own folder under `apps/`, and is a complete,
independently buildable Pebble project (its own `package.json`, `wscript`,
and `src/c/main.c`):

```
apps/
  <app-name>/
    package.json   # app metadata (uuid, targetPlatforms, ...)
    wscript        # build rules (standard Pebble SDK boilerplate)
    src/c/main.c   # the app itself
    dist/          # ready-to-install compiled .pbw (see table below)
    README.md      # what this specific app does and how to use it
```

To build any app yourself:

```sh
cd apps/<app-name>
pebble build
pebble install --emulator emery   # or --phone <ip> for a real watch
```

`scripts/build-all.sh` rebuilds every app and refreshes the `dist/*.pbw`
files linked below.

## Apps

| App | What it does | Compiled app |
|---|---|---|
| [`one-off-alarm`](apps/one-off-alarm) | Set a single wake-up alarm for an arbitrary future date/time (e.g. "in 3 weeks at 9 AM"), instead of a recurring daily alarm. | [⬇ one-off-alarm.pbw](apps/one-off-alarm/dist/one-off-alarm.pbw) |

To install a compiled app: download the `.pbw` file from the link above,
then either drag it onto the Pebble phone app, or run
`pebble install --phone <ip> <file>.pbw` / `pebble install --emulator emery <file>.pbw`.
