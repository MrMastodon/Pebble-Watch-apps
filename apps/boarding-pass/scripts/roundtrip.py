#!/usr/bin/env python3
"""Round-trips a synthetic boarding pass through the watchapp's drawing code.

Encodes a fake BCBP string, packs the module matrix the way the Android app
does, renders it through the watchapp's own code_matrix.h via render_matrix.c,
and decodes the result. If the decoded string matches the input, then packing,
unpacking and drawing all agree.

Every square symbology the watch can show is checked, since they differ in size
and the largest of them is what the module ceiling has to accommodate.

This needs no watch, no phone and no Pebble SDK. roundtrip-emulator.sh covers
the AppMessage and persistence path on top of it.

Requires: a C compiler, and `pip install zxing-cpp numpy pillow`.
"""

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
import zxingcpp

HERE = pathlib.Path(__file__).resolve().parent

# Symbol sizes the watchapp accepts, mirroring code_matrix.h.
MAX_MODULES = 45

# The symbologies the watch can draw. PDF417 is deliberately absent: at 205
# modules wide it cannot be shown on a 200 px screen at any usable scale, so the
# phone app re-encodes those as Aztec instead.
SQUARE_FORMATS = {
    "aztec": zxingcpp.BarcodeFormat.Aztec,
    "qr": zxingcpp.BarcodeFormat.QRCode,
    "datamatrix": zxingcpp.BarcodeFormat.DataMatrix,
}


def synthetic_bcbp() -> str:
    """A syntactically valid IATA BCBP M1 string built from invented values.

    Real boarding pass data carries a booking reference and a frequent flyer
    number in the clear, so it must never be committed to this repo. This one
    is the same length (131 characters) as the passes the app was built
    against, which is what decides the Aztec symbol size.
    """
    mandatory = (
        "M"                       # format code
        "1"                       # number of legs
        "TESTER/SYNTHETIC    "    # passenger name, 20
        "E"                       # electronic ticket indicator
        "ZZ9XY9 "                 # booking reference, 7
        "OSL" "CPH" "SK "         # from, to, operating carrier
        "4174 "                   # flight number, 5
        "250"                     # date of flight, day of year
        "Y"                       # compartment
        "012A"                    # seat
        "0034 "                   # check-in sequence number
        "1"                       # passenger status
    )
    assert len(mandatory) == 58, len(mandatory)

    unique = (
        "0"      # passenger description
        "W"      # source of check-in
        "W"      # source of boarding pass issuance
        "5180"   # date of issue, julian
        "B"      # document type
        "SK "    # boarding pass issuer
    )
    assert len(unique) == 11, len(unique)

    repeated = (
        "117"                 # airline numeric code
        "0000000000"          # document form and serial number
        "0"                   # selectee indicator
        "0"                   # international documentation verification
        "SK "                 # marketing carrier
        "SK "                 # frequent flyer airline
        "0000000000000000"    # frequent flyer number
        " "                   # ID/AD indicator
        "20K"                 # free baggage allowance
        "N"                   # fast track
    )
    assert len(repeated) == 42, len(repeated)

    airline_use = "SYNTHETIC   "
    conditional = (
        ">5"
        + f"{len(unique):02X}" + unique
        + f"{len(repeated):02X}" + repeated
        + airline_use
    )
    bcbp = mandatory + f"{len(conditional):02X}" + conditional
    assert len(bcbp) == 131, len(bcbp)
    return bcbp


def encode(text: str, symbology: str) -> np.ndarray:
    """Returns the module matrix as a boolean array, True = black."""
    # Data Matrix defaults to a rectangular symbol, which the watch cannot draw;
    # the Android app forces the square shape the same way.
    options = {"force_square": True} if symbology == "datamatrix" else {}
    barcode = zxingcpp.create_barcode(text, SQUARE_FORMATS[symbology], **options)
    image = np.array(zxingcpp.write_barcode_to_image(barcode, scale=1))
    if image.ndim == 3:
        image = image[:, :, 0]
    if image.shape[0] != image.shape[1]:
        raise SystemExit(f"expected a square symbol, got {image.shape}")
    return image == 0


def pack(matrix: np.ndarray) -> bytes:
    """Packs row by row, MSB first - the layout the watchapp unpacks."""
    return np.packbits(matrix.reshape(-1)).tobytes()


def render(modules: int, packed: bytes, workdir: pathlib.Path) -> np.ndarray:
    """Draws the matrix through the watchapp's own geometry, as a grey canvas."""
    binary = workdir / "render_matrix"
    subprocess.run(
        ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-o", str(binary), str(HERE / "render_matrix.c")],
        check=True,
    )

    matrix_file = workdir / "matrix.bin"
    matrix_file.write_bytes(packed)
    pgm = subprocess.run(
        [str(binary), str(modules), str(matrix_file)],
        check=True, capture_output=True,
    ).stdout

    magic, dimensions, _maxval, pixels = pgm.split(b"\n", 3)
    if magic != b"P5":
        raise SystemExit("renderer did not produce a binary PGM")
    width, height = (int(value) for value in dimensions.split())
    return np.frombuffer(pixels, dtype=np.uint8).reshape(height, width)


def decode(image: np.ndarray) -> str | None:
    result = zxingcpp.read_barcode(image, formats=[
        zxingcpp.BarcodeFormat.Aztec,
        zxingcpp.BarcodeFormat.QRCode,
        zxingcpp.BarcodeFormat.DataMatrix,
    ])
    return result.text if result else None


def write_appmessage_js(path: pathlib.Path, modules: int, packed: bytes, label: str) -> None:
    """Writes a PebbleKit JS sender, so the emulator test can push a matrix.

    The shipped app has no JS at all - the phone app owns that side. This file
    exists only inside the throwaway copy roundtrip-emulator.sh builds, and it
    exercises the same AppMessage keys the Android app uses.
    """
    matrix_js = ", ".join(str(byte) for byte in packed)
    path.write_text(f"""// Generated by roundtrip.py for the emulator test. Not part of the app.
var MODULES = {modules};
var LABEL = {json.dumps(label)};
var MATRIX = [{matrix_js}];

function send() {{
  Pebble.sendAppMessage(
    {{ '4': 1, '1': MODULES, '2': MATRIX, '3': LABEL }},
    function () {{ console.log('test matrix sent'); }},
    function (e) {{ console.log('send failed: ' + JSON.stringify(e)); }}
  );
}}

// The watchapp opens its inbox during init; retry once in case 'ready' beat it.
Pebble.addEventListener('ready', function () {{
  send();
  setTimeout(send, 2000);
}});
""")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--text", default=None,
        help="string to round-trip (defaults to a synthetic BCBP string)",
    )
    parser.add_argument(
        "--text-file", type=pathlib.Path,
        help="read the string to round-trip from this file instead",
    )
    parser.add_argument(
        "--write-text", type=pathlib.Path,
        help="write the string being used here, for a later --check run",
    )
    parser.add_argument(
        "--write-js", type=pathlib.Path,
        help="write a PebbleKit JS sender for the emulator test here",
    )
    parser.add_argument(
        "--check", type=pathlib.Path,
        help="decode this screenshot and compare it against the string, "
             "instead of rendering on the host",
    )
    parser.add_argument(
        "--symbology", choices=sorted(SQUARE_FORMATS), default=None,
        help="check only this symbology (default: all of them)",
    )
    parser.add_argument(
        "--save-png", type=pathlib.Path,
        help="also write the rendered symbol here, for eyeballing",
    )
    args = parser.parse_args()

    if args.text is not None:
        text = args.text
    elif args.text_file is not None:
        text = args.text_file.read_text()
    else:
        text = synthetic_bcbp()

    if args.write_text:
        args.write_text.write_text(text)

    if args.check:
        from PIL import Image
        screenshot = np.array(Image.open(args.check).convert("L"))
        decoded = decode(screenshot)
        if decoded is None:
            print(f"FAIL: no barcode found in {args.check}")
            return 1
        if decoded != text:
            print("FAIL: the screenshot decodes to a different string")
            print(f"  in:  {text!r}")
            print(f"  out: {decoded!r}")
            return 1
        print(f"OK: {args.check} decodes back to the original string")
        return 0

    symbologies = [args.symbology] if args.symbology else sorted(SQUARE_FORMATS)
    failed = False

    for symbology in symbologies:
        matrix = encode(text, symbology)
        modules = matrix.shape[0]
        packed = pack(matrix)
        summary = (f"{symbology:11s} {len(text)} characters -> {modules}x{modules} modules, "
                   f"{len(packed)} packed bytes")

        if modules > MAX_MODULES:
            print(f"{summary}\n  FAIL: more than the {MAX_MODULES} modules the watch can hold")
            failed = True
            continue

        with tempfile.TemporaryDirectory() as tmp:
            canvas = render(modules, packed, pathlib.Path(tmp))

        if args.save_png:
            from PIL import Image
            Image.fromarray(canvas).save(
                args.save_png.with_stem(f"{args.save_png.stem}-{symbology}")
                if len(symbologies) > 1 else args.save_png
            )

        decoded = decode(canvas)
        if decoded is None:
            print(f"{summary}\n  FAIL: the rendered symbol could not be decoded at all")
            failed = True
        elif decoded != text:
            print(f"{summary}\n  FAIL: decoded string differs from the input")
            failed = True
        else:
            print(f"{summary}  OK")

        # The emulator fixture only needs one symbology; Aztec is the smallest.
        if args.write_js and symbology == "aztec":
            write_appmessage_js(args.write_js, modules, packed, "SK4174 12A")
            print(f"wrote {args.write_js}")

    if failed:
        return 1
    print("OK: every symbology round-trips through the watch's own drawing code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
