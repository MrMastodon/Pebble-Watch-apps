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

// Evidence about what the worker saw overnight. APP_LOG only exists while a
// computer is tethered, so without this a night that produced nothing is
// completely silent about why.
#define PERSIST_KEY_DIAG 4

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

// What became of the most recent measurement. Values are persisted, so existing
// ones must keep their meaning.
typedef enum {
  HRV_OUTCOME_NONE = 0,       // no measurement has been attempted yet
  HRV_OUTCOME_LOGGED = 1,     // enough intervals; RMSSD recorded
  HRV_OUTCOME_TOO_FEW = 2,    // ran, but never got enough clean readings
  HRV_OUTCOME_DISABLED = 3,   // aborted because measuring was switched off
  HRV_OUTCOME_OFF_WRIST = 4,  // the sensor reported readings, but all off-wrist
} HrvOutcome;

// Enough to tell the failure modes apart without a tethered computer: a worker
// that was not running, sleep that was never detected, restful sleep that never
// registered, a sensor that delivered nothing, or readings too sparse to use.
typedef struct __attribute__((__packed__)) {
  uint32_t worker_started_at;    // when the worker last initialised
  uint32_t last_tick_at;         // last time the worker was demonstrably alive
  uint32_t sleep_last_seen_at;   // last HealthActivitySleep
  uint32_t restful_last_seen_at; // last HealthActivityRestfulSleep
  uint32_t last_outcome_at;      // when the last measurement ended
  uint16_t sleep_events;         // HealthEventSleepUpdate callbacks received
  uint16_t episodes;             // measurements started
  uint16_t hrv_events;           // HealthEventHRVUpdate callbacks received
  uint16_t last_sample_count;    // usable intervals in the last measurement
  uint8_t last_outcome;          // HrvOutcome
  uint8_t hrv_request_ok;        // did health_service_set_hrv_sample_period() succeed
  // Appended rather than inserted, so the byte offsets above stay put.
  //
  // A peak-to-peak interval of zero does not mean "no reading yet", whatever
  // the SDK documentation says. In the Pebble Time 2 driver (gh3x2x.c) a real
  // reading skips any interval <= 0, and a zero is emitted from exactly one
  // place: the branch taken when the watch believes it is not being worn. So a
  // measurement full of zeroes is a wrist-contact problem, not a sparse signal,
  // and saying so is far more use than "too few readings".
  uint16_t hrv_zero_events;      // HRV events carrying no interval (off-wrist)
} HrvDiagnostics;

// Writing on every tick would mean hundreds of flash writes a night for a field
// that only needs to show the worker was alive. Meaningful changes are written
// as they happen; this is just the heartbeat in between.
#define HRV_DIAG_HEARTBEAT_SEC 900
