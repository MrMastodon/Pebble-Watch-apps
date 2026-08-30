# Pebble-Watch-apps

A collection of small, standalone watchapps for the Pebble Time 2 ("emery"
platform), built with [Core Devices'](https://developer.repebble.com/) Pebble
SDK. Each app solves one narrow, self-contained problem, with no shared code
between them. Most need no companion app; where one is unavoidable it lives
alongside the watchapp in the same folder.

> **Note:** this repo is vibe-coded — written by prompting Claude rather than
> hand-written line by line. Code and READMEs are reviewed and built/tested
> before being committed, but keep that in mind if something looks off.

## Project layout

Every app lives in its own folder under `apps/`, and is usually a complete,
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

An app that needs a phone-side companion keeps the Pebble project one level
down in `watchapp/`, next to the companion app, so the two halves stay
together:

```
apps/
  <app-name>/
    watchapp/      # the Pebble project, laid out as above
    android/       # the companion app
    dist/          # ready-to-install compiled .pbw
    README.md
```

To build any app yourself:

```sh
cd apps/<app-name>      # or apps/<app-name>/watchapp
pebble build
pebble install --emulator emery   # or --phone <ip> for a real watch
```

`scripts/build-all.sh` rebuilds every app and refreshes the `dist/*.pbw`
files linked below.

## Apps

| App | What it does | Compiled app |
|---|---|---|
| [`one-off-alarm`](apps/one-off-alarm) | Set a single wake-up alarm for an arbitrary future date/time (e.g. "in 3 weeks at 9 AM"), instead of a recurring daily alarm. | [⬇ one-off-alarm.pbw](apps/one-off-alarm/dist/one-off-alarm.pbw) |
| [`boarding-pass`](apps/boarding-pass) | Show an airline boarding pass as a scannable Aztec code, pushed from an Android companion app that reads it out of a screenshot. Not yet tried on a real watch. | [⬇ boarding-pass.pbw](apps/boarding-pass/dist/boarding-pass.pbw) |

To install a compiled app: download the `.pbw` file from the link above,
then either drag it onto the Pebble phone app, or run
`pebble install --phone <ip> <file>.pbw` / `pebble install --emulator emery <file>.pbw`.
