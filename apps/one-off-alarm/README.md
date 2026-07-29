# One-Off Alarm

A watchapp for setting wake-up alarms on specific future dates and times —
for example "wake me up in 3 days at 09:00" — rather than the recurring daily
alarms of the built-in Alarms app. Several can be pending at once.

It uses the Pebble [Wakeup API](https://developer.repebble.com/guides/events-and-services/wakeups/)
to schedule the watch to relaunch this app at the chosen moment and vibrate,
even if the app has been closed in the meantime. It also subscribes to
wakeup events while running (`wakeup_service_subscribe`), so an alarm still
fires correctly even if you happen to have the app open at the exact moment
it's due — not just when it's launched fresh.

## Using it

Opening the app shows the pending alarms, soonest first:

```
   Alarms - hold to delete
  ──────────────────────
   Wed 15 Aug 09:00
   in 1d 4h - Medium
  ──────────────────────
   Fri 17 Aug 06:30
   in 3d 1h - Aggressive
  ──────────────────────
   + New alarm
  ──────────────────────
```

- **UP / DOWN** — move through the list.
- **SELECT** on `+ New alarm` — open the setup screen described below.
- **SELECT (hold)** on an alarm — delete it. A short press deliberately does
  nothing, so a stray tap can't discard an alarm.

To change an existing alarm, delete it and add a new one.

### Adding an alarm

The setup screen looks like this, with `>` marking the row you're editing:

```
      New Alarm

  > In 3 days
    At      09:00
    Vibe    Medium

   Wed 15 Aug 09:00
```

- **UP / DOWN** — change the value of the highlighted row.
- **SELECT (short press)** — move to the next field (day → hour → minute → Vibe).
- **SELECT (hold)** — add the alarm and return to the list.

Note that the day is **relative** ("Today", "Tomorrow", "In 3 days") while the
time is an **absolute** clock time ("At 09:00") — so "In 3 days / At 09:00"
means 9 in the morning, three days from now, not "3 days and 9 hours from
now". The bottom line always resolves it to a full date so you can check
before confirming. Hour and minute share the `At` row; brackets show which of
the two you're editing.

The last field, **Vibe**, picks how the alarm vibrates when it fires — Mild,
Medium, or Aggressive (each with its own pattern and repeat interval).
Changing it with UP/DOWN immediately plays that pattern once, so you can
feel the difference before committing to it.

When an alarm fires, any button dismisses it. If nobody does, it stops
vibrating after 5 minutes rather than draining the battery, and the screen
switches to "Alarm Rang / at 09:00" so you can see that it went off and when.
A fired alarm removes itself from the list; the others stay.

While any alarm is pending, the watch's app menu also shows a live countdown
under the app's name (e.g. "1d 4h left") using the [App Glance](https://developer.repebble.com/guides/user-interfaces/appglance-c/)
API. It always tracks the **next** alarm to ring, only appears while something
is actually scheduled, and disappears on its own the moment that alarm fires.

## Limitations

- Up to 8 alarms can be pending at once — the platform's ceiling on scheduled
  wakeup events per app. Beyond that the app says so rather than failing oddly.
- Alarms can't be edited, only deleted and re-added.
- The watch must stay charged and paired; a wakeup is a scheduled OS event,
  not a countdown that survives a watch factory reset.
- Per the Wakeup API, an alarm can't be scheduled within 30 seconds of the
  current time — the app will tell you if you pick a time too close to "now".
- The OS reserves a one-minute window around every scheduled wakeup, across
  all apps. Two alarms therefore can't sit within a minute of each other, and
  another app can occupy a minute too. Either way the app says "Time slot
  taken" — pick the next slot instead.
- When the list is empty, any leftover reservations from this app are cleared
  before scheduling, so an orphaned one can't block you. That cleanup is
  deliberately skipped once alarms exist, since it would remove them too.

## Build & install

```sh
cd apps/one-off-alarm
pebble build
pebble install --emulator emery   # or: pebble install --phone <ip>  for a real watch
```
