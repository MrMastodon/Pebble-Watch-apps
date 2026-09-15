#pragma once

#include <stdint.h>

// Constants shared between the foreground app (src/c/main.c) and the background
// worker (worker_src/c/worker.c). The two are compiled as separate binaries but
// share one persistent storage area, so the keys below have to agree between
// them or the on/off toggle and the history screen silently stop matching what
// the worker is doing.

// Whether HRV measurement is armed. Absent on first run, which is treated as
// enabled - the app is useless until it has measured something, so defaulting
// to off would mean a wasted first night for anyone who just installs it.
#define PERSIST_KEY_HRV_ENABLED 1

// The measurement history: a packed array of HrvRecord, oldest first, and the
// number of records currently in it.
#define PERSIST_KEY_HISTORY 2
#define PERSIST_KEY_HISTORY_COUNT 3

// DataLogging tag for the measurement log, ASCII "HRV1". Each record is two
// 4-byte unsigned ints: the UTC timestamp the measurement ended, then RMSSD in
// whole milliseconds.
#define HRV_LOG_TAG 0x48525631

// How long one measurement runs, and how often the sensor is asked for a new
// peak-to-peak interval. Both are deliberately fixed rather than adaptive: a
// measurement is only worth anything if it can be compared against the ones
// from previous nights, and duration and sampling density both shift RMSSD.
#define HRV_MEASURE_DURATION_SEC 120
#define HRV_SAMPLE_PERIOD_SEC 1

// One stored measurement. Six bytes rather than eight: RMSSD is tens to low
// hundreds of milliseconds, so 16 bits is ample, and the saving is what lets a
// useful number of records fit in a single persist value.
typedef struct __attribute__((__packed__)) {
  uint32_t timestamp;  // UTC, when the measurement ended
  uint16_t rmssd_ms;   // RMSSD, whole milliseconds
} HrvRecord;

// A persist value tops out at PERSIST_DATA_MAX_LENGTH (256 bytes), so the whole
// history fits in one key at this size - no splitting across keys, no partial
// writes to reason about. At two or three restful sleep episodes a night this
// is roughly a fortnight.
#define HRV_HISTORY_CAPACITY 40

// Oldest first, so the newest record is always the last one. That costs a
// memmove per measurement, which happens a handful of times a night, and buys
// not having to track a head index in a second persist key that could fall out
// of step with the array.
#define HRV_HISTORY_BYTES (HRV_HISTORY_CAPACITY * sizeof(HrvRecord))
