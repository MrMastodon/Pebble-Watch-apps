// Runs the real worker (worker_src/c/worker.c, compiled unchanged against
// pebble_worker.h) through a simulated night and reports what it did: how
// many times it wrote to flash, how long it held the sensor at the measuring
// rate, and what it measured.
//
// The night: asleep 23:10-06:50, restful sleep in four episodes, one of them
// shorter than the two-minute window. Before bed, two Measure now requests:
// one left to finish at 22:30, one cancelled after 30 s at 22:40. Outside a measurement the HRV broadcast
// arrives about every 42 s, as it does on a real watch all day; while the
// worker has asked for a one-second period, every second.
#include <stdarg.h>
#include <stdlib.h>
#include "pebble_worker.h"
#include "../../src/common/hrv_common.h"

static time_t s_now;
time_t sim_time(time_t *out) { if (out) *out = s_now; return s_now; }

static int s_verbose;
void sim_log(int level, const char *fmt, ...) {
  if (!s_verbose) return;
  va_list ap; va_start(ap, fmt);
  printf("  [%02ld:%02ld:%02ld] ", (long)((s_now / 3600) % 24), (long)((s_now / 60) % 60), (long)(s_now % 60));
  vprintf(fmt, ap); printf("\n"); va_end(ap);
}

// ---- storage, with a count of every write
#define KEYS 16
static uint8_t s_store[KEYS][256];
static int s_store_len[KEYS];
static int s_writes[KEYS];
bool persist_exists(uint32_t key) { return s_store_len[key] > 0; }
int persist_read_data(uint32_t key, void *buf, size_t size) {
  int n = s_store_len[key] < (int)size ? s_store_len[key] : (int)size;
  memcpy(buf, s_store[key], n); return n;
}
int persist_write_data(uint32_t key, const void *data, size_t size) {
  if (size > 256) { printf("FAIL: persist write of %zu bytes\n", size); exit(1); }
  memcpy(s_store[key], data, size); s_store_len[key] = (int)size; s_writes[key]++; return (int)size;
}
bool persist_read_bool(uint32_t key) { return s_store[key][0] != 0; }
int32_t persist_read_int(uint32_t key) { int32_t v; memcpy(&v, s_store[key], 4); return v; }
int persist_write_int(uint32_t key, int32_t value) { return persist_write_data(key, &value, 4); }
int persist_delete(uint32_t key) { s_store_len[key] = 0; return 0; }

// ---- health
static HealthEventHandler s_health;
static HealthActivityMask s_activities;
static uint16_t s_period;
static long s_period_seconds;          // seconds spent with a period held
bool health_service_events_subscribe(HealthEventHandler h, void *c) { s_health = h; return true; }
bool health_service_events_unsubscribe(void) { s_health = NULL; return true; }
HealthActivityMask health_service_peek_current_activities(void) { return s_activities; }
bool health_service_set_hrv_sample_period(uint16_t seconds) { s_period = seconds; return true; }
static uint16_t s_ppi = 900;
uint16_t health_service_peek_hrv_ppi_ms(void) { return s_ppi; }

// ---- ticks
static TickHandler s_tick; static TimeUnits s_unit;
void tick_timer_service_subscribe(TimeUnits u, TickHandler h) { s_tick = h; s_unit = u; }
void tick_timer_service_unsubscribe(void) { s_tick = NULL; }

static AppWorkerMessageHandler s_msg;
bool app_worker_message_subscribe(AppWorkerMessageHandler h) { s_msg = h; return true; }

static int s_logged;
DataLoggingSessionRef data_logging_create(uint32_t t, DataLoggingItemType ty, uint16_t s, bool r) { return (void *)1; }
DataLoggingResult data_logging_log(DataLoggingSessionRef s, const void *d, uint32_t n) { s_logged++; return DATA_LOGGING_SUCCESS; }

// ---- the night
typedef struct { int from, to; } Span;   // seconds since 22:00
#define H(h, m) ((((h) + 2) % 24) * 3600 + (m) * 60)   // clock time -> seconds since 22:00
static const Span ASLEEP = { H(23, 10), H(6, 50) };
static const Span RESTFUL[] = { { H(0, 0), H(0, 40) }, { H(2, 0), H(2, 30) },
                                { H(3, 30), H(3, 31) },   // one minute: ends inside the window
                                { H(5, 0), H(5, 25) } };
static int in(Span s, int t) { return t >= s.from && t < s.to; }

void worker_event_loop(void) {
  const time_t start = 1790000000 - (1790000000 % 86400) + 22 * 3600;   // 22:00 UTC
  const int night = 10 * 3600;                                         // until 08:00
  HealthActivityMask last = 0;
  for (int t = 0; t < night; t++) {
    s_now = start + t;
    HealthActivityMask a = 0;
    if (in(ASLEEP, t)) a |= HealthActivitySleep;
    for (unsigned i = 0; i < sizeof(RESTFUL) / sizeof(RESTFUL[0]); i++) {
      if (in(RESTFUL[i], t)) a |= HealthActivityRestfulSleep;
    }
    s_activities = a;
    if (s_period) s_period_seconds++;
    // The health service reports sleep changes when they happen and a
    // significant update every half hour.
    if (a != last && s_health) s_health(HealthEventSleepUpdate, NULL);
    if (t % 1800 == 0 && s_health) s_health(HealthEventSignificantUpdate, NULL);
    last = a;
    // HRV readings: every second while a period is held, else every 42 s.
    if (s_health && (s_period ? 1 : (t % 42 == 0))) {
      s_ppi = (uint16_t)(900 + (t % 5) * 8);
      s_health(HealthEventHRVUpdate, NULL);
    }
    if (s_msg && (t == H(22, 30) || t == H(22, 40))) {
      AppWorkerMessage m = { .data0 = WORKER_CMD_MEASURE_NOW };
      s_msg(WORKER_MSG_FROM_APP, &m);
    }
    if (s_msg && t == H(22, 40) + 30) {
      AppWorkerMessage m = { .data0 = WORKER_CMD_CANCEL };
      s_msg(WORKER_MSG_FROM_APP, &m);
    }
    if (s_tick && (s_unit == SECOND_UNIT || t % 60 == 0)) {
      struct tm tm = { 0 };
      s_tick(&tm, s_unit);
    }
  }
}

int worker_main(void);

int main(int argc, char **argv) {
  s_verbose = argc > 1;
  worker_main();
  HrvRecord history[HRV_HISTORY_CAPACITY];
  int count = persist_read_data(PERSIST_KEY_HISTORY, history, sizeof(history)) / (int)sizeof(HrvRecord);
  int total = 0;
  for (int k = 0; k < KEYS; k++) total += s_writes[k];
  printf("measurements stored: %d\n", count);
  printf("sensor held at the measuring rate: %ld s\n", s_period_seconds);
  printf("sample period held after the night: %u\n", (unsigned)s_period);
  printf("flash writes: %d total, of which diagnostics %d, history %d\n", total,
         s_writes[PERSIST_KEY_DIAG], s_writes[PERSIST_KEY_HISTORY] + s_writes[PERSIST_KEY_HISTORY_COUNT]);
  int manual = 0;
  for (int i = 0; i < count; i++) {
    bool m = (history[i].rejected & HRV_REJECTED_MANUAL_FLAG) != 0;
    manual += m;
    printf("  record %d: %u ms, rejected %u%s\n", i, history[i].rmssd_ms,
           history[i].rejected & HRV_REJECTED_COUNT_MASK, m ? " (manual)" : "");
  }

  // What this night must come out as. The flash budget is generous - the point
  // is to catch a return to writing on every tick, which was over 900 a night.
  // Measure now writes every second on purpose, so the app can show progress:
  // 150 of the writes here are the two requests (120 s and 30 s).
  int failures = 0;
  #define EXPECT(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); failures++; } } while (0)
  EXPECT(count == 5, "five measurements: four restful episodes and one Measure now");
  EXPECT(manual == 1, "exactly one of them flagged manual (the cancelled one stores nothing)");
  EXPECT(s_logged == 0, "nothing sent to data logging");
  EXPECT(s_period == 0, "sensor released at the end");
  EXPECT(s_period_seconds == 4 * 120 + 60 + 30, "sensor held only for the measurement windows");
  EXPECT(s_writes[PERSIST_KEY_DIAG] <= 150 + 100, "diagnostics: Measure now's 150 plus at most 100 for the night");
  printf(failures ? "%d checks failed\n" : "all checks passed\n", failures);
  return failures ? 1 : 0;
}
