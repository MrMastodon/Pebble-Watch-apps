#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Geometry and bit layout of the barcode module matrix the phone app sends.
//
// The watch is deliberately symbology-agnostic: Aztec, QR and Data Matrix all
// arrive here as the same square grid of black and white modules, and the phone
// decides which of them to send. Nothing below needs to know the difference.
//
// Deliberately free of pebble.h: the host-side round-trip test renders through
// these same functions, so what the test decodes is what the watch draws rather
// than a second implementation that can quietly drift from it.

// The smallest symbol worth expecting (a compact Aztec is 15 modules).
#define CODE_MIN_MODULES 15

// The ceiling is set by persistent storage, not by the screen: one persist key
// holds 256 bytes, and ceil(45*45/8) = 254. That also happens to be about the
// finest a scanner manages off a 202 ppi reflective display, at 4 px per
// module. A 131 character boarding pass needs 37 modules as Aztec, 40 as Data
// Matrix or 41 as QR, so this leaves room for longer passes.
#define CODE_MAX_MODULES 45
#define CODE_MAX_MATRIX_BYTES 256

// Pixels kept clear on the left and right at the largest scale. Symbols are
// centred in both directions inside the drawing area, so the white around them
// is the quiet zone QR and Data Matrix need. The phone app mirrors the scaling
// rule below to decide whether a symbology's quiet zone actually fits before it
// sends one, so the watch never has to know which symbology it is drawing.
#define CODE_SIDE_MARGIN 14
#define CODE_MAX_PX_PER_MODULE 5

static inline size_t code_matrix_bytes(int modules) {
  return ((size_t)modules * (size_t)modules + 7) / 8;
}

// Bits run row by row, MSB first within each byte - the layout the phone app
// packs into.
static inline bool code_module_is_set(const uint8_t *matrix, int modules, int row, int col) {
  const uint32_t index = (uint32_t)row * (uint32_t)modules + (uint32_t)col;
  return (matrix[index / 8] >> (7 - (index % 8))) & 1;
}

static inline int code_pixels_per_module(int width, int modules) {
  const int fitted = (width - CODE_SIDE_MARGIN) / modules;
  return fitted < CODE_MAX_PX_PER_MODULE ? fitted : CODE_MAX_PX_PER_MODULE;
}

// Finds the next horizontal run of set modules in `row`, starting the search at
// *col and leaving *col past the end of the run. Returns false once the row is
// exhausted. Callers fill a whole run with one rectangle: a 37x37 symbol is
// 1369 modules but only a few hundred runs.
static inline bool code_next_run(const uint8_t *matrix, int modules, int row,
                                 int *col, int *run_start, int *run_length) {
  int c = *col;
  while (c < modules && !code_module_is_set(matrix, modules, row, c)) {
    c++;
  }
  if (c >= modules) {
    *col = c;
    return false;
  }
  const int start = c;
  while (c < modules && code_module_is_set(matrix, modules, row, c)) {
    c++;
  }
  *run_start = start;
  *run_length = c - start;
  *col = c;
  return true;
}
