// A stand-in for the Pebble worker SDK, just large enough to compile
// worker_src/c/worker.c on a computer and run it through a simulated night.
// Every call the worker makes is recorded or answered by sim.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// The worker's notion of "now" is the simulation's clock.
time_t sim_time(time_t *out);
#define time(p) sim_time(p)

#define APP_LOG_LEVEL_ERROR 1
#define APP_LOG_LEVEL_INFO 3
void sim_log(int level, const char *fmt, ...);
#define APP_LOG(level, ...) sim_log(level, __VA_ARGS__)

// Persistent storage.
bool persist_exists(uint32_t key);
int persist_read_data(uint32_t key, void *buffer, size_t size);
int persist_write_data(uint32_t key, const void *data, size_t size);
bool persist_read_bool(uint32_t key);
int32_t persist_read_int(uint32_t key);
int persist_write_int(uint32_t key, int32_t value);
int persist_delete(uint32_t key);

// Health.
typedef uint32_t HealthActivityMask;
#define HealthActivitySleep ((HealthActivityMask)1)
#define HealthActivityRestfulSleep ((HealthActivityMask)2)
typedef enum {
  HealthEventSignificantUpdate,
  HealthEventMovementUpdate,
  HealthEventSleepUpdate,
  HealthEventHRVUpdate,
} HealthEventType;
typedef void (*HealthEventHandler)(HealthEventType event, void *context);
bool health_service_events_subscribe(HealthEventHandler handler, void *context);
bool health_service_events_unsubscribe(void);
HealthActivityMask health_service_peek_current_activities(void);
bool health_service_set_hrv_sample_period(uint16_t seconds);
uint16_t health_service_peek_hrv_ppi_ms(void);

// Ticks.
typedef enum { SECOND_UNIT = 1, MINUTE_UNIT = 2 } TimeUnits;
typedef void (*TickHandler)(struct tm *tick_time, TimeUnits units_changed);
void tick_timer_service_subscribe(TimeUnits unit, TickHandler handler);
void tick_timer_service_unsubscribe(void);

// Messages from the app.
typedef struct { uint16_t data0, data1, data2; } AppWorkerMessage;
typedef void (*AppWorkerMessageHandler)(uint16_t type, AppWorkerMessage *data);
bool app_worker_message_subscribe(AppWorkerMessageHandler handler);

// DataLogging.
typedef void *DataLoggingSessionRef;
typedef enum { DATA_LOGGING_BYTE_ARRAY, DATA_LOGGING_UINT, DATA_LOGGING_INT } DataLoggingItemType;
typedef enum { DATA_LOGGING_SUCCESS = 0, DATA_LOGGING_BUSY, DATA_LOGGING_FULL } DataLoggingResult;
DataLoggingSessionRef data_logging_create(uint32_t tag, DataLoggingItemType type, uint16_t size, bool resume);
DataLoggingResult data_logging_log(DataLoggingSessionRef session, const void *data, uint32_t count);

void worker_event_loop(void);
