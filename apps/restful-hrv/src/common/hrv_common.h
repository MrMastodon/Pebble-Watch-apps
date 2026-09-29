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
//
// Moved from keys 2 and 3 when records grew from six bytes to eight. Reading
// the old array with the new layout would produce plausible-looking garbage,
// so the old keys are deleted rather than migrated - they held values computed
// without artefact filtering, which are not comparable with the new ones
// anyway.
#define PERSIST_KEY_HISTORY 5
#define PERSIST_KEY_HISTORY_COUNT 6
#define PERSIST_KEY_HISTORY_V1 2
#define PERSIST_KEY_HISTORY_COUNT_V1 3

// Evidence about what the worker saw overnight. APP_LOG only exists while a
// computer is tethered, so without this a night that produced nothing is
// completely silent about why.
#define PERSIST_KEY_DIAG 4

// Builds before 1.9 also logged every measurement to DataLogging, under the
// tags "HRV1" and "HRV2". Nothing in this project ever read that log, so it was
// removed rather than keep handing health data to a channel no one uses.

// How long one measurement runs, and how often the sensor is asked for a new
// peak-to-peak interval. Both are deliberately fixed rather than adaptive: a
// measurement is only worth anything if it can be compared against the ones
// from previous nights, and duration and sampling density both shift RMSSD.
#define HRV_MEASURE_DURATION_SEC 120
#define HRV_SAMPLE_PERIOD_SEC 1

// Intervals the buffer can hold. This must not be the binding constraint on a
// measurement: if it fills, the window stops being 120 seconds and quietly
// becomes "the first N beats", which varies in duration with heart rate and
// destroys the night-to-night comparability the whole app exists for.
//
// One slot per second was not enough - real hardware delivered around 136
// intervals in a 120-second window at rest, so the buffer filled and the rest
// were discarded. 300 covers a sustained 150 bpm, well above anything that
// happens during restful sleep, for 600 bytes of a worker that has room.
#define HRV_PPI_CAPACITY 300

// Artefact filter: an interval is rejected when it differs from the previous
// accepted one by more than this percentage (the Malik criterion, 20 %).
//
// Without it, a single beat the sensor misses doubles one interval, and the
// two huge successive differences around it dominate the whole sum of squares:
// a calm window of about 30 ms reads as well over 100. Beat-to-beat changes
// during restful sleep are nowhere near 20 %, so genuine variation is not
// touched - only the mechanical errors.
#define HRV_ARTEFACT_TOLERANCE_PCT 20

// One stored measurement, eight bytes: RMSSD is tens to low hundreds of
// milliseconds, so 16 bits is ample, and keeping records this small is what
// lets a useful number of them fit in a single persist value.
typedef struct __attribute__((__packed__)) {
  uint32_t timestamp;  // UTC, when the measurement ended
  uint16_t rmssd_ms;   // RMSSD, whole milliseconds, after artefact filtering
  uint16_t rejected;   // intervals the artefact filter threw away, plus the flag below
} HrvRecord;

// Set in HrvRecord.rejected for a measurement started with Measure now rather
// than by restful sleep. The count itself can never come near this bit - there
// are at most HRV_PPI_CAPACITY intervals to reject - so it costs no space and
// keeps the record, and the link format built from it, exactly as it was. The
// settings page uses it to keep daytime spot checks out of the nightly figures.
#define HRV_REJECTED_MANUAL_FLAG 0x8000
#define HRV_REJECTED_COUNT_MASK 0x7FFF

// A persist value tops out at PERSIST_DATA_MAX_LENGTH (256 bytes), which is
// exactly 32 records. The history is spread over four such chunks, 128 records
// in all: about a month at three or four restful sleep episodes a night. That
// is how long the watch can go without the app being opened - the only moment
// the history can reach the phone - before measurements are lost.
//
// Chunk 0 is PERSIST_KEY_HISTORY, the key that held the whole history when it
// was a single chunk, so a history written by an earlier build is read as it
// is. Keys 7-9 hold the rest.
#define HRV_HISTORY_CHUNK 32
#define HRV_HISTORY_CHUNKS 4
#define HRV_HISTORY_CAPACITY (HRV_HISTORY_CHUNK * HRV_HISTORY_CHUNKS)
#define HRV_HISTORY_CHUNK_BYTES (HRV_HISTORY_CHUNK * sizeof(HrvRecord))
#define HRV_HISTORY_KEYS { PERSIST_KEY_HISTORY, 7, 8, 9 }

// Oldest first, so the newest record is always the last one. When the history
// is full, the oldest whole chunk is dropped at once rather than one record at
// a time: an ordinary append then rewrites only the chunk it lands in, and the
// shift that rewrites every chunk happens once every 32 measurements. The watch
// therefore holds between 96 and 128 measurements once it has filled up.

// What became of the most recent measurement. Values are persisted, so existing
// ones must keep their meaning.
typedef enum {
  HRV_OUTCOME_NONE = 0,       // no measurement has been attempted yet
  HRV_OUTCOME_LOGGED = 1,     // enough intervals; RMSSD recorded
  HRV_OUTCOME_TOO_FEW = 2,    // ran, but never got enough clean readings
  HRV_OUTCOME_DISABLED = 3,   // aborted because measuring was switched off
  HRV_OUTCOME_NO_INTERVALS = 4,  // readings arrived, none carried an interval
  // Written and flushed before the result is computed, so that a worker which
  // dies doing the arithmetic leaves this behind instead of looking as though
  // the measurement never ended. Seeing it persist is the bug report.
  HRV_OUTCOME_COMPUTING = 5,
  HRV_OUTCOME_CANCELLED = 6,  // a Measure now the user cancelled; nothing stored
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
  // reading skips any interval <= 0, so the only code path that emits a zero is
  // the one taken when the watch does not believe it is being worn.
  //
  // That is not the same as the watch being off your wrist. The BPM path gates
  // on the same flag - a heart rate is only filled in when it is set - so a
  // night with a heart rate graph is a night when it was set at least some of
  // the time. Counting these separately is what will show whether it flickers.
  uint16_t hrv_zero_events;      // ...of those, ones that carried no interval

  // The HRV broadcast in hrm_manager.c is an unconditional event_put - it is
  // not filtered by what the receiving app asked for, unlike the raw HRM
  // stream. So hrv_events above counts every reading the sensor produced all
  // day, whoever caused it, and says nothing about our own measurement windows.
  // This is the one that does.
  uint16_t hrv_events_measuring;  // HRV events that arrived during a measurement
  uint16_t last_rejected;         // intervals the artefact filter removed, last time
} HrvDiagnostics;

// Messages from the app to the worker: run a measurement now, without waiting
// for restful sleep, or cancel one started that way.
#define WORKER_MSG_FROM_APP 0
#define WORKER_CMD_MEASURE_NOW 1
#define WORKER_CMD_CANCEL 2

// Writing on every tick would mean hundreds of flash writes a night for a field
// that only needs to show the worker was alive. Meaningful changes are written
// as they happen; this is just the heartbeat in between.
#define HRV_DIAG_HEARTBEAT_SEC 900
