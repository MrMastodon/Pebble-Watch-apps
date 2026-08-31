// Renders a packed module matrix the way the watchapp's code layer does, to a
// binary PGM on stdout. Used by roundtrip.py to check - without a watch or an
// emulator - that a symbol survives packing, unpacking and drawing.
//
// It deliberately shares code_matrix.h with the watchapp, so a change to the
// bit layout or the scaling has to break this test rather than slip through.
//
//   render_matrix <modules> <matrix.bin> > out.pgm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../watchapp/src/c/code_matrix.h"

// The code layer on emery: the full 200 px width, and the window height minus
// the 26 px reserved for the label.
#define CANVAS_WIDTH 200
#define CANVAS_HEIGHT 202

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: render_matrix <modules> <matrix.bin>\n");
    return 2;
  }

  const int modules = atoi(argv[1]);
  if (modules < CODE_MIN_MODULES || modules > CODE_MAX_MODULES) {
    fprintf(stderr, "module count %d outside %d..%d\n", modules,
            CODE_MIN_MODULES, CODE_MAX_MODULES);
    return 1;
  }

  uint8_t matrix[CODE_MAX_MATRIX_BYTES];
  FILE *in = fopen(argv[2], "rb");
  if (!in) {
    perror(argv[2]);
    return 1;
  }
  const size_t expected = code_matrix_bytes(modules);
  const size_t read = fread(matrix, 1, sizeof(matrix), in);
  fclose(in);
  if (read != expected) {
    fprintf(stderr, "expected %zu matrix bytes, got %zu\n", expected, read);
    return 1;
  }

  static uint8_t canvas[CANVAS_HEIGHT][CANVAS_WIDTH];
  memset(canvas, 0xFF, sizeof(canvas));

  const int px = code_pixels_per_module(CANVAS_WIDTH, modules);
  const int size = px * modules;
  const int ox = (CANVAS_WIDTH - size) / 2;
  const int oy = (CANVAS_HEIGHT - size) / 2;

  for (int row = 0; row < modules; row++) {
    int col = 0, run_start = 0, run_length = 0;
    while (code_next_run(matrix, modules, row, &col, &run_start, &run_length)) {
      for (int y = oy + row * px; y < oy + (row + 1) * px; y++) {
        for (int x = ox + run_start * px; x < ox + (run_start + run_length) * px; x++) {
          if (y >= 0 && y < CANVAS_HEIGHT && x >= 0 && x < CANVAS_WIDTH) {
            canvas[y][x] = 0x00;
          }
        }
      }
    }
  }

  printf("P5\n%d %d\n255\n", CANVAS_WIDTH, CANVAS_HEIGHT);
  fwrite(canvas, 1, sizeof(canvas), stdout);
  return 0;
}
