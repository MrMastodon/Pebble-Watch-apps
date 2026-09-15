#pragma once

// Constants shared between the foreground app (src/c/main.c) and the background
// worker (worker_src/c/worker.c). The two are compiled as separate binaries but
// share one persistent storage area, so the key below has to agree between them
// or the on/off toggle silently stops reaching the worker.

// Whether HRV measurement is armed. Absent on first run, which is treated as
// enabled - the app is useless until it has measured something, so defaulting
// to off would mean a wasted first night for anyone who just installs it.
#define PERSIST_KEY_HRV_ENABLED 1

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
