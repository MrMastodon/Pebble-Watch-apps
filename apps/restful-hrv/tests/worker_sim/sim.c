// Runs the real worker (worker_src/c/worker.c, compiled unchanged against
// pebble_worker.h) through simulated scenarios and checks what it did: what
// it stored, how long it held the sensor at the measuring rate, and how often
// it wrote to flash. Each scenario runs in a process of its own, so the
// worker starts from scratch every time:
//
//   night     a whole night: asleep 23:10-06:50, four restful episodes (one
//             shorter than the window), and before bed one Measure now left
//             to finish and one cancelled after 30 s
//   zero      a Measure now with the strap off (every reading empty), then one
//             where no readings arrive at all - two different failures
//   clock     the clock is set back ten minutes during a Measure now
//   overlap   restful sleep begins while a Measure now is running
//   capacity  140 Measure now in a row, more than the watch can hold
//   corrupt   the stored history is shorter than its count says
#include <stdarg.h>
#include <stdlib.h>
#include "pebble_worker.h"
#include "../../src/common/hrv_common.h"

static const char *s_scenario = "night";
static int s_verbose;

static time_t s_now;
time_t sim_time(time_t *out) { if (out) *out = s_now; return s_now; }

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
static int s_failures;
#define EXPECT(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

bool persist_exists(uint32_t key) { return s_store_len[key] > 0; }
int persist_read_data(uint32_t key, void *buf, size_t size) {
  int n = s_store_len[key] < (int)size ? s_store_len[key] : (int)size;
  memcpy(buf, s_store[key], n); return n;
}
int persist_write_data(uint32_t key, const void *data, size_t size) {
  if (size > 256) { printf("FAIL: persist write of %zu bytes to key %u\n", size, key); exit(1); }
  memcpy(s_store[key], data, size); s_store_len[key] = (int)size; s_writes[key]++; return (int)size;
}
bool persist_read_bool(uint32_t key) { return s_store[key][0] != 0; }
int32_t persist_read_int(uint32_t key) { int32_t v; memcpy(&v, s_store[key], 4); return v; }
int persist_write_int(uint32_t key, int32_t value) { return persist_write_data(key, &value, 4); }
int persist_delete(uint32_t key) { s_store_len[key] = 0; return 0; }

static int total_writes(void) { int n = 0; for (int k = 0; k < KEYS; k++) n += s_writes[k]; return n; }

// ---- health
static HealthEventHandler s_health;
static HealthActivityMask s_activities;
static uint16_t s_period;
static long s_period_seconds;          // seconds spent with a period held
static uint16_t s_ppi = 900;
bool health_service_events_subscribe(HealthEventHandler h, void *c) { s_health = h; return true; }
bool health_service_events_unsubscribe(void) { s_health = NULL; return true; }
HealthActivityMask health_service_peek_current_activities(void) { return s_activities; }
bool health_service_set_hrv_sample_period(uint16_t seconds) { s_period = seconds; return true; }
uint16_t health_service_peek_hrv_ppi_ms(void) { return s_ppi; }

// ---- ticks, app messages
static TickHandler s_tick; static TimeUnits s_unit;
void tick_timer_service_subscribe(TimeUnits u, TickHandler h) { s_tick = h; s_unit = u; }
void tick_timer_service_unsubscribe(void) { s_tick = NULL; }
static AppWorkerMessageHandler s_msg;
bool app_worker_message_subscribe(AppWorkerMessageHandler h) { s_msg = h; return true; }

static int s_logged;
DataLoggingSessionRef data_logging_create(uint32_t t, DataLoggingItemType ty, uint16_t s, bool r) { return (void *)1; }
DataLoggingResult data_logging_log(DataLoggingSessionRef s, const void *d, uint32_t n) { s_logged++; return DATA_LOGGING_SUCCESS; }

// ---- reading back what the worker stored
static HrvRecord s_hist[HRV_HISTORY_CAPACITY];
static int load_history(void) {
  int count = persist_exists(PERSIST_KEY_HISTORY_COUNT) ? persist_read_int(PERSIST_KEY_HISTORY_COUNT) : 0;
#ifdef HRV_HISTORY_CHUNK
  static const uint32_t keys[] = HRV_HISTORY_KEYS;
  for (int c = 0; c * HRV_HISTORY_CHUNK < count; c++) {
    persist_read_data(keys[c], &s_hist[c * HRV_HISTORY_CHUNK], HRV_HISTORY_CHUNK * sizeof(HrvRecord));
  }
#else
  persist_read_data(PERSIST_KEY_HISTORY, s_hist, sizeof(s_hist));
#endif
  return count;
}
static HrvDiagnostics diag(void) {
  HrvDiagnostics d; memset(&d, 0, sizeof(d));
  persist_read_data(PERSIST_KEY_DIAG, &d, sizeof(d)); return d;
}
static void send(uint16_t cmd) {
  AppWorkerMessage m = { .data0 = cmd };
  if (s_msg) s_msg(WORKER_MSG_FROM_APP, &m);
}

// One simulated second: activities, health events, HRV readings, ticks.
// `beats` 0 = no readings at all while measuring, 1 = normal, 2 = all empty.
static int s_beats = 1;
static void step(time_t now, HealthActivityMask a, int t) {
  static HealthActivityMask last;
  s_now = now;
  s_activities = a;
  if (s_period) s_period_seconds++;
  if (a != last && s_health) s_health(HealthEventSleepUpdate, NULL);
  if (t % 1800 == 0 && s_health) s_health(HealthEventSignificantUpdate, NULL);
  last = a;
  // HRV readings: every second while a period is held, else every 42 s.
  bool reading = s_period ? (s_beats != 0) : (t % 42 == 0);
  if (s_health && reading) {
    s_ppi = (s_period && s_beats == 2) ? 0 : (uint16_t)(900 + (t % 5) * 8);
    s_health(HealthEventHRVUpdate, NULL);
  }
  if (s_tick && (s_unit == SECOND_UNIT || t % 60 == 0)) {
    struct tm tm = { 0 };
    s_tick(&tm, s_unit);
  }
}

static const time_t START = 1790000000 - (1790000000 % 86400) + 22 * 3600;   // 22:00 UTC
typedef struct { int from, to; } Span;   // seconds since 22:00
#define H(h, m) ((((h) + 2) % 24) * 3600 + (m) * 60)
static int in(Span s, int t) { return t >= s.from && t < s.to; }

static void scenario_night(void) {
  const Span asleep = { H(23, 10), H(6, 50) };
  const Span restful[] = { { H(0, 0), H(0, 40) }, { H(2, 0), H(2, 30) },
                           { H(3, 30), H(3, 31) }, { H(5, 0), H(5, 25) } };
  for (int t = 0; t < 10 * 3600; t++) {
    HealthActivityMask a = 0;
    if (in(asleep, t)) a |= HealthActivitySleep;
    for (unsigned i = 0; i < sizeof(restful) / sizeof(restful[0]); i++) {
      if (in(restful[i], t)) a |= HealthActivityRestfulSleep;
    }
    if (t == H(22, 30) || t == H(22, 40)) send(WORKER_CMD_MEASURE_NOW);
    if (t == H(22, 40) + 30) send(WORKER_CMD_CANCEL);
    step(START + t, a, t);
  }
}

static uint8_t s_outcomes[4];
static void scenario_zero(void) {
  for (int t = 0; t < 600; t++) {
    if (t == 10) { s_beats = 2; send(WORKER_CMD_MEASURE_NOW); }    // strap off
    if (t == 200) { s_outcomes[0] = diag().last_outcome; }
    if (t == 300) { s_beats = 0; send(WORKER_CMD_MEASURE_NOW); }   // sensor silent
    if (t == 490) { s_outcomes[1] = diag().last_outcome; }
    step(START + t, 0, t);
  }
}

static long s_released_after = -1;
static void scenario_clock(void) {
  int started = 10;
  for (int t = 0; t < 900; t++) {
    if (t == started) send(WORKER_CMD_MEASURE_NOW);
    // The watch's clock jumps back ten minutes 30 s into the measurement.
    time_t now = START + t - (t >= started + 30 ? 600 : 0);
    step(now, 0, t);
    if (t > started && s_period == 0 && s_released_after < 0) s_released_after = t - started;
  }
}

static void scenario_overlap(void) {
  // Asleep throughout; a Measure now at 00:59:00, restful sleep from 01:00
  // to 01:30 - it begins while the manual measurement is still running.
  const Span restful = { H(1, 0), H(1, 30) };
  for (int t = H(0, 30); t < H(2, 0); t++) {
    HealthActivityMask a = HealthActivitySleep | (in(restful, t) ? HealthActivityRestfulSleep : 0);
    if (t == H(0, 59)) send(WORKER_CMD_MEASURE_NOW);
    step(START + t, a, t);
  }
}

static int s_append_writes_max;
static void scenario_capacity(void) {
  int t = 0;
  s_now = START;
  for (int n = 0; n < 140; n++) {
    send(WORKER_CMD_MEASURE_NOW);
    int before = total_writes() - s_writes[PERSIST_KEY_DIAG];
    for (int i = 0; i < 125; i++, t++) step(START + t, 0, t);
    int w = total_writes() - s_writes[PERSIST_KEY_DIAG] - before;
    // The shift that drops the oldest chunk rewrites every chunk; count the
    // ordinary appends separately.
    if (w <= 3 && w > s_append_writes_max) s_append_writes_max = w;
  }
}

// 40 measurements, then the second chunk loses all but 3 of its 8 records
// while the count still says 40. The next measurement must keep the 35 that
// are readable instead of starting again from nothing.
static int s_corrupt_before;
static void scenario_corrupt(void) {
  static const uint32_t keys[] = HRV_HISTORY_KEYS;
  int t = 0;
  s_now = START;
  for (int n = 0; n < 41; n++) {
    if (n == 40) {
      s_store_len[keys[1]] = 3 * sizeof(HrvRecord);
      s_corrupt_before = persist_read_int(PERSIST_KEY_HISTORY_COUNT);
    }
    send(WORKER_CMD_MEASURE_NOW);
    for (int i = 0; i < 125; i++, t++) step(START + t, 0, t);
  }
}

void worker_event_loop(void) {
  if (!strcmp(s_scenario, "night")) scenario_night();
  else if (!strcmp(s_scenario, "zero")) scenario_zero();
  else if (!strcmp(s_scenario, "clock")) scenario_clock();
  else if (!strcmp(s_scenario, "overlap")) scenario_overlap();
  else if (!strcmp(s_scenario, "capacity")) scenario_capacity();
  else if (!strcmp(s_scenario, "corrupt")) scenario_corrupt();
  else { printf("unknown scenario %s\n", s_scenario); exit(2); }
}

int worker_main(void);

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v")) s_verbose = 1; else s_scenario = argv[i];
  }
  printf("scenario: %s\n", s_scenario);
  worker_main();

  int count = load_history();
  int manual = 0;
  for (int i = 0; i < count; i++) manual += (s_hist[i].rejected & HRV_REJECTED_MANUAL_FLAG) != 0;
  printf("  measurements stored: %d (%d manual)\n", count, manual);
  printf("  sensor held at the measuring rate: %ld s; held after the end: %u\n", s_period_seconds, (unsigned)s_period);
  printf("  flash writes: %d, of which diagnostics %d\n", total_writes(), s_writes[PERSIST_KEY_DIAG]);

  EXPECT(s_period == 0, "sensor released at the end");
  EXPECT(s_logged == 0, "nothing sent to data logging");

  if (!strcmp(s_scenario, "night")) {
    EXPECT(count == 5, "five measurements: four restful episodes and one Measure now");
    EXPECT(manual == 1, "exactly one flagged manual (the cancelled one stores nothing)");
    EXPECT(s_period_seconds == 4 * 120 + 60 + 30, "sensor held only for the measurement windows");
    // Generous: the point is to catch writing on every tick (over 900 a
    // night). Measure now writes every 2 s on purpose so the app can show
    // progress: 75 of these are the two requests (120 s and 30 s).
    EXPECT(s_writes[PERSIST_KEY_DIAG] <= 75 + 100, "diagnostics: Measure now's share plus at most 100 for the night");
  } else if (!strcmp(s_scenario, "zero")) {
    printf("  outcomes: strap off -> %u, no readings -> %u\n", s_outcomes[0], s_outcomes[1]);
    EXPECT(s_outcomes[0] == HRV_OUTCOME_NO_INTERVALS, "empty readings during the measurement: 'no intervals'");
    EXPECT(s_outcomes[1] == HRV_OUTCOME_TOO_FEW, "no readings at all: 'too few', not 'no intervals'");
    EXPECT(count == 0, "nothing stored");
  } else if (!strcmp(s_scenario, "clock")) {
    printf("  sensor released %ld s after the start\n", s_released_after);
    EXPECT(s_released_after > 0 && s_released_after <= 126, "sensor released within the window despite the clock going back");
  } else if (!strcmp(s_scenario, "overlap")) {
    EXPECT(count == 2 && manual == 1, "the restful episode gets its own measurement after the Measure now");
  } else if (!strcmp(s_scenario, "capacity")) {
    bool ordered = true;
    for (int i = 1; i < count; i++) ordered &= s_hist[i].timestamp > s_hist[i - 1].timestamp;
    printf("  kept %d of 140; most writes for an ordinary append: %d\n", count, s_append_writes_max);
    EXPECT(count >= HRV_HISTORY_CAPACITY - 32 && count <= HRV_HISTORY_CAPACITY, "between capacity-32 and capacity kept");
    EXPECT(ordered, "oldest first, timestamps strictly increasing");
    EXPECT(count > 0 && (time_t)s_hist[count - 1].timestamp >= s_now - 10, "the newest measurement is the last one");
    EXPECT(s_append_writes_max <= 2, "an ordinary append writes one chunk and the count");
    EXPECT(count == 108, "128, then the oldest 32 dropped, then 12 more");
  } else if (!strcmp(s_scenario, "corrupt")) {
    bool ordered = true;
    for (int i = 1; i < count; i++) ordered &= s_hist[i].timestamp > s_hist[i - 1].timestamp;
    printf("  count said %d, chunk 1 cut to 3 records; after one more: %d\n", s_corrupt_before, count);
    EXPECT(count == 32 + 3 + 1, "the readable records kept, plus the new one");
    EXPECT(ordered, "still oldest first");
  }
  printf(s_failures ? "  %d checks failed\n" : "  all checks passed\n", s_failures);
  return s_failures ? 1 : 0;
}
