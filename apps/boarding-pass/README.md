# Boarding Pass

Shows an airline boarding pass barcode on a Pebble Time 2 as a scannable 2D
code, so the gate scanner can read it off your wrist instead of your phone.

It comes in two halves:

| Part | What it does |
|---|---|
| [`watchapp/`](watchapp) | Pebble C app for `emery`. Draws a module matrix and nothing else - it never learns which symbology it is showing. |
| [`android/`](android) | Companion app. Finds and decodes the barcode in a screenshot, re-encodes it, and pushes the matrix to the watch. |

## How it works

```
Airline app shows a barcode
      |
      | you screenshot it and share it with the Android app
      v
Android app
  1. finds the symbol in the screenshot  -> a centred candidate region
  2. ZXing decodes it                    -> BCBP string + symbology
  3. ZXing re-encodes the string         -> square BitMatrix
  4. packs the matrix                    -> 172 bytes for a 37x37 Aztec
  5. PebbleKit 2 sends it in one AppMessage
      |
      v
Watchapp
  1. stores the matrix in persistent storage
  2. draws 4x4 or 5x5 px rectangles, one per module
  3. turns the backlight on while the code is up
```

All the barcode work happens on the phone. The watch never encodes anything,
never receives an image, and never learns which symbology it is drawing - it
gets finished bits, 172 of them for a normal 131 character boarding pass. That
fits in a single AppMessage and a single 256 byte persist key, which is what
lets the watch draw the pass at startup without the phone anywhere nearby.

## Symbologies

All four 2D symbologies IATA Resolution 792 allows on a boarding pass are read.
Three of them are square, so the watch can show them as they were issued:

| Symbology | 131 character pass | On the watch |
|---|---|---|
| Aztec | 37x37 | 5 px per module |
| Data Matrix | 40x40, forced square | 4 px per module |
| QR | 41x41 | 4 px per module |
| PDF417 | 205x44 | cannot be drawn |

PDF417 - what most US carriers issue - is 205 modules wide. On a 200 px screen
that is under one pixel per module, so there is no scale at which it can be
shown. The app reads it and offers to send the same boarding pass data as Aztec
instead, but asks first: gate scanners are imaging readers and IATA allows
either symbology, yet that is not something to discover at a gate. The prompt
has a "do not ask me again" option.

Everything else keeps the symbology the airline issued, so the gate reader sees
exactly the symbol it was given. The watch centres the symbol, and the phone
checks before sending that the white around it is wide enough for that
symbology's quiet zone - four modules for QR, one for Data Matrix, none for
Aztec.

The screenshot only has to be taken once per booking. Two screenshots of the
same pass taken minutes apart decode to the same string, so airlines are not
rotating the code per view; a seat change or a re-check-in means sharing a fresh
screenshot.

## What the watch is told

Deliberately, the watch never sees the passenger name or the booking reference.
The label at the bottom of the screen is built from the flight fields alone.

| AppMessage key | Type | Contents |
|---|---|---|
| 1 | uint8 | module count `n` (15..45) |
| 2 | byte array | packed matrix, `ceil(n*n/8)` bytes, row by row, MSB first |
| 3 | string | short label such as `SK4174 12A` |
| 4 | uint8 | protocol version, currently 1 |
| 5 | uint8 | when 1, forget the stored pass; no other key is read |

## Using it

1. Build and sideload the watchapp (see below), or install
   [`dist/boarding-pass.pbw`](dist/boarding-pass.pbw).
2. Build and install the Android app.
3. Screenshot the barcode in your airline's app, then share the screenshot with
   **Boarding Pass** from the share sheet. (The in-app picker works too, for a
   screenshot you took earlier.) A plain full-screen screenshot is fine - there
   is no need to crop it down to the barcode.
4. That is it - the phone opens **Boarding Pass** on the watch and sends the
   code there. Opening it yourself later draws the code straight from the
   watch's own storage, with no phone needed.

Only a watchapp that is in the foreground can receive an AppMessage, and a
watchface counts as a different app, so the phone opens the watchapp on the
wrist rather than asking you to. It also pushes the latest copy whenever the
watchapp opens by any means, so the watch cannot end up showing a pass the phone
has since replaced.

Deleting the pass in the phone app deletes it from the watch too. That one does
not open the watchapp on your wrist - popping an app up only to tell it to
forget something is not worth it - so if the watch is not showing the app the
phone remembers and sends the deletion the next time the watchapp opens.
Importing a new pass in the meantime cancels the pending deletion; a stored pass
always wins over a deletion left over from before it.

On the watch, **select** toggles the backlight. A reflective screen sometimes
scans better with it off under strong light, so that is a choice rather than
something the app decides for you.

## Privacy

- The Android app has no `INTERNET` permission at all. Not "unused" - absent,
  so a boarding pass provably cannot leave the phone over the network.
- The BCBP string carries the booking reference and the frequent flyer number in
  the clear. It is stored encrypted under an AES-GCM key that lives in the
  Android Keystore, and is never logged in any build.
- Deleting it from the app's main screen deletes it from the watch as well, so
  a pass that has been thrown away does not linger on the wrist.
- Backups and device transfer are switched off for the app's data.

## Building

### Watchapp

```sh
cd watchapp
pebble build
pebble install --emulator emery      # or --phone <ip>
```

### Android app

Needs a JDK 17 or newer and an Android SDK with API 37 (PebbleKit 2 requires
callers to compile against 37).

```sh
cd android
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

The debug build installs as `com.pebblewatchapps.boardingpass.debug`, which the
watchapp's `package.json` also lists as a companion app, so a debug build can
talk to the watch without uninstalling a release one.

## Tests

### Round trip, no hardware

```sh
pip install zxing-cpp numpy pillow
scripts/roundtrip.py
```

Encodes a synthetic boarding pass in each square symbology, packs it, renders it
through the watchapp's own `code_matrix.h` (the renderer includes that header
rather than reimplementing it), and decodes the result. If the string that comes
out matches the one that went in, then packing, unpacking and drawing all agree.

### Round trip on the emulator

```sh
scripts/roundtrip-emulator.sh
```

Builds a throwaway copy of the watchapp with a PebbleKit JS sender attached,
pushes a synthetic pass over the same AppMessage keys the Android app uses,
screenshots the emulator and decodes the screenshot. This covers the message
format and persistence on top of the drawing. The shipped app has no JS - the
sender only ever exists inside the temporary copy.

### Android unit tests

```sh
cd android && ./gradlew test
```

Covers the bit packing against ZXing's own `BitMatrix`, the symbol size ceiling
and the quiet zone rule, the BCBP label extraction (including that it leaks
neither the name nor the booking reference), and what storage keeps and drops -
a deletion the watch has not heard about has to survive being cleared, while a
newly imported pass has to cancel it. What the phone pushes when the watchapp
opens is a separate set of tests, since that is the only moment it can push
anything and getting the precedence wrong strands the watch showing something
the phone no longer has.

Reading an image is covered too, under Robolectric in native graphics mode, so
`BitmapFactory` really decodes and `getPixels` really reads back. The main test
draws every symbology into a full 1080x2340 screenshot at six positions and two
sizes - 48 cases - and asserts each one comes back with the right payload and
the right symbology.

That test exists because finding the symbol turned out to be most of the work.
QR and PDF417 are found wherever they sit; Aztec and Data Matrix are not,
because both detectors search outwards from the centre of the image they are
given. Measured over the same 108 case matrix: the whole image alone read 64,
sliding windows read 79, and searching for high-contrast blobs and centring each
one in its own canvas read all 108 - in less time than the windows took.

All test data is synthetic. Real BCBP strings must never be committed here.

## Not yet verified

The Aztec path has been used end to end, phone to watch. What has not been tried
is a real gate scanner - that is the final test and cannot be simulated - and
the QR, Data Matrix and PDF417 paths have only ever been exercised against
generated images, never against a boarding pass a real airline issued. Do a dry
run with a barcode scanner app before relying on any of them.
