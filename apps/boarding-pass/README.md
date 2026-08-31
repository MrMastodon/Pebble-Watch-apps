# Boarding Pass

Shows an airline boarding pass barcode on a Pebble Time 2 as a scannable Aztec
code, so the gate scanner can read it off your wrist instead of your phone.

It comes in two halves:

| Part | What it does |
|---|---|
| [`watchapp/`](watchapp) | Pebble C app for `emery`. Draws a module matrix and nothing else. |
| [`android/`](android) | Companion app. Decodes the barcode out of a screenshot, re-encodes it, and pushes the matrix to the watch. |

## How it works

```
Airline app shows an Aztec code
      |
      | you screenshot it and share it with the Android app
      v
Android app
  1. ZXing decodes the Aztec symbol      -> BCBP string
  2. ZXing re-encodes the string         -> 37x37 BitMatrix
  3. packs the matrix                    -> 172 bytes
  4. PebbleKit 2 sends it in one AppMessage
      |
      v
Watchapp
  1. stores the matrix in persistent storage
  2. draws 5x5 px rectangles, one per module
  3. turns the backlight on while the code is up
```

All the Aztec work happens on the phone. The watch never encodes a barcode and
never receives an image - only finished bits, 172 of them for a normal 131
character boarding pass. That fits in a single AppMessage and a single 256 byte
persist key, which is what lets the watch draw the pass at startup without the
phone anywhere nearby.

The screenshot only has to be taken once per booking. Two screenshots of the
same pass taken minutes apart decode to the same string, so airlines are not
rotating the code per view; a seat change or a re-check-in means sharing a fresh
screenshot.

## What the watch is told

Deliberately, the watch never sees the passenger name or the booking reference.
The label at the bottom of the screen is built from the flight fields alone.

| AppMessage key | Type | Contents |
|---|---|---|
| 1 | uint8 | module count `n` (15..41) |
| 2 | byte array | packed matrix, `ceil(n*n/8)` bytes, row by row, MSB first |
| 3 | string | short label such as `SK4174 12A` |
| 4 | uint8 | protocol version, currently 1 |

## Using it

1. Build and sideload the watchapp (see below), or install
   [`dist/boarding-pass.pbw`](dist/boarding-pass.pbw).
2. Build and install the Android app.
3. Screenshot the barcode in your airline's app, then share the screenshot with
   **Boarding Pass** from the share sheet. (The in-app picker works too, for a
   screenshot you took earlier.)
4. Open **Boarding Pass** on the watch. The code is drawn immediately from
   storage; the phone app also pushes the latest copy whenever the watchapp
   opens, so the watch cannot end up showing a stale pass.

On the watch, **select** toggles the backlight. A reflective screen sometimes
scans better with it off under strong light, so that is a choice rather than
something the app decides for you.

## Privacy

- The Android app has no `INTERNET` permission at all. Not "unused" - absent,
  so a boarding pass provably cannot leave the phone over the network.
- The BCBP string carries the booking reference and the frequent flyer number in
  the clear. It is stored encrypted under an AES-GCM key that lives in the
  Android Keystore, is never logged in any build, and can be deleted from the
  app's main screen.
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

Encodes a synthetic boarding pass, packs it, renders it through the watchapp's
own `aztec_matrix.h` (the renderer includes that header rather than
reimplementing it), and decodes the result. If the string that comes out matches
the one that went in, then packing, unpacking and drawing all agree.

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

Covers the bit packing against ZXing's own `BitMatrix`, the symbol size ceiling,
and the BCBP label extraction (including that it leaks neither the name nor the
booking reference).

Reading an image is covered too, under Robolectric in native graphics mode, so
`BitmapFactory` really decodes and `getPixels` really reads back: the test draws
an Aztec symbol into a screenshot-shaped PNG and asserts the string comes back
out. That path had shipped broken once, because nothing exercised it.

All test data is synthetic. Real BCBP strings must never be committed here.

## Not yet verified

The watchapp compiles and its drawing code passes the host round trip, but it
has not yet been run on a real watch or in the emulator, and the Android app has
not been run on a phone. Scanning at an actual gate is the final test and cannot
be simulated - do a dry run with a barcode scanner app before relying on it.
