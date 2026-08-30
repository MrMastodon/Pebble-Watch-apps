#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Geometry and bit layout of the Aztec module matrix the phone app sends.
//
// Deliberately free of pebble.h: the host-side round-trip test renders through
// these same functions, so what the test decodes is what the watch draws rather
// than a second implementation that can quietly drift from it.

// Aztec symbols come in fixed sizes; 15 is the smallest compact one and 41 is
// the largest that still leaves 4 px per module on a 200 px wide screen.
// Anything finer is not reliably scannable off a 202 ppi reflective display.
#define AZTEC_MIN_MODULES 15
#define AZTEC_MAX_MODULES 41

// ceil(41*41/8) = 211, rounded up while staying under the 256 byte per-key
// limit on Pebble persistent storage.
#define AZTEC_MAX_MATRIX_BYTES 216

// Pixels kept clear on the left and right at the largest scale.
#define AZTEC_SIDE_MARGIN 14
#define AZTEC_SYMBOL_TOP 8
#define AZTEC_MAX_PX_PER_MODULE 5

static inline size_t aztec_matrix_bytes(int modules) {
  return ((size_t)modules * (size_t)modules + 7) / 8;
}

// Bits run row by row, MSB first within each byte - the layout the phone app
// packs into.
static inline bool aztec_module_is_set(const uint8_t *matrix, int modules, int row, int col) {
  const uint32_t index = (uint32_t)row * (uint32_t)modules + (uint32_t)col;
  return (matrix[index / 8] >> (7 - (index % 8))) & 1;
}

static inline int aztec_pixels_per_module(int width, int modules) {
  const int fitted = (width - AZTEC_SIDE_MARGIN) / modules;
  return fitted < AZTEC_MAX_PX_PER_MODULE ? fitted : AZTEC_MAX_PX_PER_MODULE;
}

// Finds the next horizontal run of set modules in `row`, starting the search at
// *col and leaving *col past the end of the run. Returns false once the row is
// exhausted. Callers fill a whole run with one rectangle: a 37x37 symbol is
// 1369 modules but only a few hundred runs.
static inline bool aztec_next_run(const uint8_t *matrix, int modules, int row,
                                  int *col, int *run_start, int *run_length) {
  int c = *col;
  while (c < modules && !aztec_module_is_set(matrix, modules, row, c)) {
    c++;
  }
  if (c >= modules) {
    *col = c;
    return false;
  }
  const int start = c;
  while (c < modules && aztec_module_is_set(matrix, modules, row, c)) {
    c++;
  }
  *run_start = start;
  *run_length = c - start;
  *col = c;
  return true;
}
