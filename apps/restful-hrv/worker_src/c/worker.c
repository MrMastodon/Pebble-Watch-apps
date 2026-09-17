#include <pebble_worker.h>
#include <math.h>
#include <string.h>

#include "../../src/common/hrv_common.h"

// Measures heart rate variability while the watch reports restful sleep, and
// hands each result to DataLogging so the phone picks it up.
//
// The point of this worker is night-to-night comparability, so every
// measurement is taken the same way: the same fixed window, the same fixed
// sample period, started at the same moment in a sleep episode (its beginning).
// Nothing here adapts to signal quality or battery - a measurement that varies
// in how it was taken cannot be compared against last night's.
//
// One measurement is taken per restful sleep episode. Several episodes in a
// night therefore produce several records, which is intended: they are separate
// observations, not one nightly average.

// One slot per second of the measurement window, which is as many readings as
// a one-second sample period can be expected to produce. The collector bounds
// its writes against this anyway rather than trusting that expectation.
#define PPI_BUFFER_SIZE HRV_MEASURE_DURATION_SEC

// Below this many intervals the RMSSD is noise rather than a measurement -
// typically the sensor never got a clean read through the night's wrist
// position. Such an episode is dropped rather than logged, so a bad night shows
// up as a gap in the data instead of a plausible-looking wrong number.
#define MIN_VALID_SAMPLES 10

// Restful sleep is polled as well as subscribed to, because health events fire
// on the health service's own schedule and a restful episode could otherwise
// start or end minutes before the worker noticed. While idle a minute of
// latency is harmless; during a measurement the tick doubles as the window's
// clock, so it runs per second.
#define IDLE_TICK_UNIT MINUTE_UNIT
#define MEASURING_TICK_UNIT SECOND_UNIT

// Defined below; the measurement start/stop helpers re-subscribe the tick.
static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed);

static DataLoggingSessionRef s_log_session;

// Evidence for the diagnostics screen. Held in RAM and flushed on change, so a
// night that produced no measurements can still say which step it got stuck on.
static HrvDiagnostics s_diag;
static time_t s_diag_written_at;

static void prv_diag_flush(void) {
  persist_write_data(PERSIST_KEY_DIAG, &s_diag, sizeof(s_diag));
  s_diag_written_at = time(NULL);
}

static void prv_diag_load(void) {
  if (persist_exists(PERSIST_KEY_DIAG)) {
    int read = persist_read_data(PERSIST_KEY_DIAG, &s_diag, sizeof(s_diag));
    // A short read means the struct changed shape since it was written; the
    // counters would be meaningless, so start clean rather than misreport.
    if (read != (int)sizeof(s_diag)) {
      memset(&s_diag, 0, sizeof(s_diag));
    }
  } else {
    memset(&s_diag, 0, sizeof(s_diag));
  }
}

static uint16_t s_ppi_buffer[PPI_BUFFER_SIZE];
static uint16_t s_ppi_count;

static bool s_measuring;
static time_t s_measure_start;

// A measurement the user asked for from the app, rather than one restful sleep
// triggered. It runs the same code on the same sensor subscription - the only
// difference is that it is not cancelled by not being asleep.
static bool s_manual;

// Last seen state of the restful sleep bit. A measurement starts on the
// transition into restful sleep, not on the bit merely being set, so that
// finishing a measurement mid-episode does not immediately start another.
static bool s_was_restful;

static bool prv_hrv_enabled(void) {
  if (!persist_exists(PERSIST_KEY_HRV_ENABLED)) {
    return true;
  }
  return persist_read_bool(PERSIST_KEY_HRV_ENABLED);
}

// RMSSD: the root mean square of successive differences between peak-to-peak
// intervals, in whole milliseconds. Returns 0 when there is not enough data to
// compute one, which the caller treats as "do not log".
static uint32_t prv_compute_rmssd_ms(void) {
  if (s_ppi_count < MIN_VALID_SAMPLES) {
    return 0;
  }

  double sum_sq_diff = 0;
  int count = 0;
  for (uint16_t i = 1; i < s_ppi_count; i++) {
    // Widened before multiplying, not after: the difference of two 16-bit
    // intervals squared does not fit in 32 bits at the top of the range.
    double diff = (double)s_ppi_buffer[i] - (double)s_ppi_buffer[i - 1];
    sum_sq_diff += diff * diff;
    count++;
  }

  if (count == 0) {
    return 0;
  }
  return (uint32_t)(sqrt(sum_sq_diff / count) + 0.5);
}

// Appends a finished measurement to the history the app's own screen reads and
// the phone's settings page is fed from. This is a second copy of what goes to
// DataLogging, on purpose: DataLogging data is only reachable from a native
// companion app, so without this the numbers would be invisible on the watch
// that took them.
static void prv_append_history(uint32_t timestamp, uint32_t rmssd_ms) {
  HrvRecord history[HRV_HISTORY_CAPACITY];
  int count = persist_exists(PERSIST_KEY_HISTORY_COUNT)
      ? persist_read_int(PERSIST_KEY_HISTORY_COUNT) : 0;

  // A count that disagrees with what is actually stored means the two keys fell
  // out of step - start over rather than read past the data that is really there.
  if (count < 0 || count > HRV_HISTORY_CAPACITY) {
    count = 0;
  }
  if (count > 0) {
    int read = persist_read_data(PERSIST_KEY_HISTORY, history, sizeof(history));
    if (read < (int)(count * sizeof(HrvRecord))) {
      count = 0;
    }
  }

  if (count == HRV_HISTORY_CAPACITY) {
    memmove(&history[0], &history[1], (HRV_HISTORY_CAPACITY - 1) * sizeof(HrvRecord));
    count = HRV_HISTORY_CAPACITY - 1;
  }

  history[count].timestamp = timestamp;
  // Clamped rather than truncated: a wrapped 16-bit value would read as a
  // plausible small number instead of an obviously pegged one.
  history[count].rmssd_ms = (rmssd_ms > UINT16_MAX) ? UINT16_MAX : (uint16_t)rmssd_ms;
  count++;

  persist_write_data(PERSIST_KEY_HISTORY, history, count * sizeof(HrvRecord));
  persist_write_int(PERSIST_KEY_HISTORY_COUNT, count);
}

static void prv_log_measurement(uint32_t rmssd_ms) {
  if (!s_log_session) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "HRV result dropped: no logging session");
    return;
  }
  // Two items rather than one, so each RMSSD arrives on the phone with the time
  // it was taken - DataLogging itself does not timestamp records.
  uint32_t record[2] = { (uint32_t)time(NULL), rmssd_ms };
  DataLoggingResult result = data_logging_log(s_log_session, record, 2);
  // Worth saying out loud: a full or closed session means the night's numbers
  // are being silently thrown away, and nothing else would reveal that.
  if (result != DATA_LOGGING_SUCCESS) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "HRV result dropped: data_logging_log returned %d",
            (int)result);
  }
}

static void prv_start_measurement(void) {
  s_ppi_count = 0;
  s_measure_start = time(NULL);
  s_measuring = true;

  // The return value matters: if the sensor will not grant the sample period
  // there is nothing to collect, and that is invisible from the outside.
  bool granted = health_service_set_hrv_sample_period(HRV_SAMPLE_PERIOD_SEC);
  tick_timer_service_subscribe(MEASURING_TICK_UNIT, prv_tick_handler);

  s_diag.episodes++;
  s_diag.hrv_request_ok = granted ? 1 : 0;
  prv_diag_flush();

  APP_LOG(APP_LOG_LEVEL_INFO, "HRV measurement started (sample period granted: %s)",
          granted ? "yes" : "no");
}

// Ends the current measurement, whatever the reason: the window elapsed, the
// sleep episode ended early, the user switched measuring off, or the worker is
// shutting down. Whatever was collected is logged if it is enough to be worth
// anything - a short episode still says something, an empty one does not.
static void prv_stop_measurement(void) {
  if (!s_measuring) {
    return;
  }
  s_measuring = false;
  s_manual = false;

  // Released first, so the sensor stops being driven at the measurement rate
  // even if something below misbehaves.
  health_service_set_hrv_sample_period(0);
  tick_timer_service_subscribe(IDLE_TICK_UNIT, prv_tick_handler);

  s_diag.last_sample_count = s_ppi_count;
  s_diag.last_outcome_at = (uint32_t)time(NULL);

  uint32_t rmssd_ms = prv_compute_rmssd_ms();
  if (rmssd_ms > 0) {
    s_diag.last_outcome = HRV_OUTCOME_LOGGED;
  } else if (!prv_hrv_enabled()) {
    s_diag.last_outcome = HRV_OUTCOME_DISABLED;
  } else if (s_ppi_count == 0 && s_diag.hrv_zero_events > 0) {
    // The sensor was talking to us throughout and every reading came back
    // empty. Distinct from a sparse signal, and worth saying so.
    s_diag.last_outcome = HRV_OUTCOME_NO_INTERVALS;
  } else {
    s_diag.last_outcome = HRV_OUTCOME_TOO_FEW;
  }
  prv_diag_flush();

  if (rmssd_ms > 0) {
    APP_LOG(APP_LOG_LEVEL_INFO, "HRV measurement complete: %u ms from %u intervals",
            (unsigned)rmssd_ms, (unsigned)s_ppi_count);
    // History first: it is the copy the user can actually reach from the watch,
    // so it should not depend on DataLogging having room.
    prv_append_history((uint32_t)time(NULL), rmssd_ms);
    prv_log_measurement(rmssd_ms);
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "HRV measurement discarded: %u intervals, %u off-wrist",
            (unsigned)s_ppi_count, (unsigned)s_diag.hrv_zero_events);
  }
}

// Single place where the restful sleep bit decides what happens, called both
// from health events and from the tick fallback.
static void prv_evaluate_sleep_state(void) {
  HealthActivityMask activities = health_service_peek_current_activities();
  bool is_restful = (activities & HealthActivityRestfulSleep) != 0;

  // Plain sleep is tracked purely as evidence. It is not what triggers a
  // measurement, but "asleep all night, never restful" and "never asleep at
  // all" are different problems and otherwise indistinguishable.
  uint32_t now = (uint32_t)time(NULL);
  bool changed = false;
  if (activities & HealthActivitySleep) {
    s_diag.sleep_last_seen_at = now;
    changed = true;
  }
  if (is_restful) {
    s_diag.restful_last_seen_at = now;
    changed = true;
  }
  if (changed) {
    prv_diag_flush();
  }

  if (s_measuring) {
    // A manual measurement is deliberately not tied to the sleep state, or it
    // would be cancelled on the first tick for the obvious reason.
    if (!is_restful && !s_manual) {
      // The episode ended inside the window. Logging the partial measurement is
      // better than discarding it, as long as it cleared the sample floor.
      prv_stop_measurement();
    }
  } else if (is_restful && !s_was_restful && prv_hrv_enabled()) {
    prv_start_measurement();
  }

  s_was_restful = is_restful;
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t now = time(NULL);
  s_diag.last_tick_at = (uint32_t)now;
  // Only written periodically: this field exists to show the worker was alive,
  // and a flash write every minute all night would be a real cost for that.
  if (now - s_diag_written_at >= HRV_DIAG_HEARTBEAT_SEC) {
    prv_diag_flush();
  }

  if (s_measuring) {
    // Checked every tick rather than once per episode, so switching measuring
    // off in the app takes effect immediately instead of at the next episode.
    if (!prv_hrv_enabled() && !s_manual) {
      prv_stop_measurement();
      return;
    }
    // Flushed every second while the user is watching the numbers move.
    if (s_manual) {
      prv_diag_flush();
    }
    // Measured against the wall clock rather than counted in ticks, so a tick
    // the worker misses under load does not stretch the window.
    if (time(NULL) - s_measure_start >= HRV_MEASURE_DURATION_SEC) {
      prv_stop_measurement();
      // s_was_restful deliberately left set: the episode is still running, and
      // it has already had its measurement.
      return;
    }
  }

  prv_evaluate_sleep_state();
}

static void prv_worker_message_handler(uint16_t type, AppWorkerMessage *message) {
  if (type != WORKER_MSG_FROM_APP || !message) {
    return;
  }
  if (message->data0 == WORKER_CMD_MEASURE_NOW && !s_measuring) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Manual measurement requested");
    s_manual = true;
    prv_start_measurement();
  }
}

static void prv_health_handler(HealthEventType event, void *context) {
  switch (event) {
    case HealthEventHRVUpdate:
      // Counted whether or not a measurement is running: zero here after a
      // night means the sensor never produced an interval at all, which is a
      // different fault from producing too few to use.
      // Counted separately because this one arrives whether or not we asked:
      // the health service broadcasts every HRV reading to all subscribers.
      s_diag.hrv_events++;
      if (s_measuring) {
        s_diag.hrv_events_measuring++;
      }
      if (s_measuring && s_ppi_count < PPI_BUFFER_SIZE) {
        uint16_t ppi = health_service_peek_hrv_ppi_ms();
        if (ppi > 0) {
          s_ppi_buffer[s_ppi_count++] = ppi;
        } else {
          // Counted, not just skipped: the driver only sends a zero from its
          // not-being-worn branch, so the count says how much of the window the
          // watch spent believing that - which is the measurement we lack.
          s_diag.hrv_zero_events++;
        }
      }
      break;
    case HealthEventSleepUpdate:
      s_diag.sleep_events++;
      prv_evaluate_sleep_state();
      break;
    case HealthEventSignificantUpdate:
      prv_evaluate_sleep_state();
      break;
    default:
      break;
  }
}

static void prv_init(void) {
  prv_diag_load();
  s_diag.worker_started_at = (uint32_t)time(NULL);
  s_diag.last_tick_at = s_diag.worker_started_at;
  prv_diag_flush();

  // Resumed rather than recreated, so records still waiting for the phone
  // survive the worker being restarted overnight.
  s_log_session = data_logging_create(HRV_LOG_TAG, DATA_LOGGING_UINT, 4, true);

  // A previous run that was killed rather than deinitialised may have left the
  // sensor held at the measurement rate.
  health_service_set_hrv_sample_period(0);

  health_service_events_subscribe(prv_health_handler, NULL);
  tick_timer_service_subscribe(IDLE_TICK_UNIT, prv_tick_handler);
  app_worker_message_subscribe(prv_worker_message_handler);

  // Left false even if the watch is already in restful sleep, so a worker that
  // starts or restarts mid-episode still measures that episode rather than
  // waiting for the next one.
  s_was_restful = false;
  prv_evaluate_sleep_state();
}

static void prv_deinit(void) {
  prv_stop_measurement();
  prv_diag_flush();
  // Repeated deliberately: prv_stop_measurement() is a no-op when idle, and the
  // sample period must never be left held by a worker that is going away.
  health_service_set_hrv_sample_period(0);

  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();

  // data_logging_finish() is deliberately not called. The session outlives the
  // worker on purpose - finishing it would close off records that have not
  // reached the phone yet.
}

int main(void) {
  prv_init();
  worker_event_loop();
  prv_deinit();
}
