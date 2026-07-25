# One-Off Alarm

A watchapp for setting a single wake-up alarm on a specific future date and
time — for example "wake me up in 21 days at 09:00" — rather than a
recurring daily alarm like the built-in Alarms app.

It uses the Pebble [Wakeup API](https://developer.repebble.com/guides/events-and-services/wakeups/)
to schedule the watch to relaunch this app at the chosen moment and vibrate,
even if the app has been closed in the meantime.

## Using it

- **UP / DOWN** — change the value of the highlighted field.
- **SELECT (short press)** — move to the next field (Days → Hour → Minute).
- **SELECT (hold)** — confirm and schedule the alarm.

The screen always shows the resulting absolute date/time so you can double
check it before confirming.

Once set, the app shows a countdown and a **SELECT** to cancel. When the
alarm fires, any button dismisses it.

## Limitations

- Only one alarm can be active at a time in this app (by design, to keep it simple).
- The watch must stay charged and paired; a wakeup is a scheduled OS event,
  not a countdown that survives a watch factory reset.
- Per the Wakeup API, an alarm can't be scheduled within 30 seconds of the
  current time — the app will tell you if you pick a time too close to "now".

## Build & install

```sh
cd apps/one-off-alarm
pebble build
pebble install --emulator emery   # or: pebble install --phone <ip>  for a real watch
```
