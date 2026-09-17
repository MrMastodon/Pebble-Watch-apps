#include <pebble.h>
#include <string.h>

#include "../common/hrv_common.h"

// The user interface for Restful HRV. All the measuring happens in the
// background worker (worker_src/c/worker.c), which runs whether or not this app
// is open; this side is a switch, a list of what has been measured, and the
// bridge that hands that list to the phone.
//
// Two screens: the switch, and the history behind DOWN.

#define APP_VERSION "1.4.0"

// How long after toggling to re-check whether the worker actually started or
// stopped. Both operations are asynchronous, and launching one can put a
// confirmation prompt in front of the user first, so the answer is not known
// when app_worker_launch() returns.
#define WORKER_POLL_INTERVAL_MS 500
#define WORKER_POLL_ATTEMPTS 8

// The history is sent to the phone as one byte array, so the outbox has to fit
// the whole thing plus dictionary overhead in a single message.
#define OUTBOX_SIZE (HRV_HISTORY_BYTES + sizeof(HrvDiagnostics) + 96)
#define INBOX_SIZE 64

// One retry, because the usual reason a send fails is that the phone connection
// was not up yet when the app launched.
#define SEND_RETRY_DELAY_MS 3000

// A send attempted this long after launch, independent of the phone announcing
// itself. Reopening the app usually finds PebbleKit JS already loaded, and this
// covers the case where its announcement never arrives.
#define INITIAL_SEND_DELAY_MS 2000

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_state_layer;
static TextLayer *s_worker_layer;
static TextLayer *s_hint_layer;
static TextLayer *s_version_layer;

static Window *s_history_window;
static MenuLayer *s_history_menu;
static TextLayer *s_history_empty_layer;

static Window *s_diag_window;
static ScrollLayer *s_diag_scroll;
static TextLayer *s_diag_text_layer;
static char s_diag_text[512];

static bool s_enabled;

static AppTimer *s_worker_poll_timer;
static int s_worker_polls_left;
static AppTimer *s_send_retry_timer;
static AppTimer *s_initial_send_timer;
static bool s_send_retried;

static char s_worker_text[32];

static HrvRecord s_history[HRV_HISTORY_CAPACITY];
static int s_history_count;

static bool prv_read_enabled(void) {
  if (!persist_exists(PERSIST_KEY_HRV_ENABLED)) {
    return true;
  }
  return persist_read_bool(PERSIST_KEY_HRV_ENABLED);
}

// Reads the worker's history into s_history. Kept newest-last in storage, which
// is the order the list wants reversed, so the menu indexes it backwards.
static void prv_load_history(void) {
  s_history_count = 0;

  int count = persist_exists(PERSIST_KEY_HISTORY_COUNT)
      ? persist_read_int(PERSIST_KEY_HISTORY_COUNT) : 0;
  if (count <= 0 || count > HRV_HISTORY_CAPACITY) {
    return;
  }

  int read = persist_read_data(PERSIST_KEY_HISTORY, s_history, sizeof(s_history));
  if (read < (int)(count * sizeof(HrvRecord))) {
    // The count and the array disagree; trust the bytes that are actually there.
    count = (read > 0) ? (read / (int)sizeof(HrvRecord)) : 0;
  }
  s_history_count = count;
}

// ---------------------------------------------------------------- phone bridge

// Hands the whole history to PebbleKit JS, which caches it so the settings page
// can show it later. The worker cannot do this itself - background workers have
// no AppMessage - so the data only reaches the phone while this app is open.
// Loads the worker's record of the night. Returns false when there is none.
static bool prv_load_diagnostics(HrvDiagnostics *diag) {
  memset(diag, 0, sizeof(*diag));
  if (!persist_exists(PERSIST_KEY_DIAG)) {
    return false;
  }
  return persist_read_data(PERSIST_KEY_DIAG, diag, sizeof(*diag)) == (int)sizeof(*diag);
}

// Hands the history and the worker's status to the phone, which keeps them
// somewhere the watch cannot reach.
//
// This is not only a convenience. Removing an app deletes its persistent
// storage outright, and the phone is what decides to remove it - so everything
// on the watch is one locker sync away from being gone. The copy on the phone
// is the durable one.
static void prv_send_history_to_phone(void) {
  HrvDiagnostics diag;
  bool have_diag = prv_load_diagnostics(&diag);

  // Sent even with nothing measured: a night that produced no measurements is
  // exactly when the status is worth having off the watch.
  if (s_history_count == 0 && !have_diag) {
    return;
  }

  DictionaryIterator *iter;
  AppMessageResult begin = app_message_outbox_begin(&iter);
  if (begin != APP_MSG_OK) {
    // Usually APP_MSG_BUSY from the two send triggers racing each other, which
    // is harmless - whichever one won is carrying the same data.
    APP_LOG(APP_LOG_LEVEL_INFO, "History send skipped, AppMessageResult %d", (int)begin);
    return;
  }
  if (s_history_count > 0) {
    dict_write_data(iter, MESSAGE_KEY_HrvHistory, (const uint8_t *)s_history,
                    s_history_count * sizeof(HrvRecord));
  }
  if (have_diag) {
    dict_write_data(iter, MESSAGE_KEY_HrvStatus, (const uint8_t *)&diag, sizeof(diag));
  }
  app_message_outbox_send();
}

static void prv_send_retry(void *data) {
  s_send_retry_timer = NULL;
  prv_send_history_to_phone();
}

// Belt and braces alongside the phone's announcement: two independent triggers
// for the same transfer, because the only thing worse than sending twice is a
// settings page that is silently always empty.
static void prv_initial_send(void *data) {
  s_initial_send_timer = NULL;
  prv_send_history_to_phone();
}

// PebbleKit JS starts when this app starts, so at launch it is usually not
// listening yet and an immediate send is simply lost. It announces itself
// instead, and that is what triggers the transfer.
static void prv_inbox_received_handler(DictionaryIterator *iter, void *context) {
  if (dict_find(iter, MESSAGE_KEY_PhoneReady)) {
    s_send_retried = false;
    prv_send_history_to_phone();
  }
}

static void prv_outbox_sent_handler(DictionaryIterator *iter, void *context) {
  // The only confirmation there is that the phone got the history. Without it,
  // "the settings page is empty" has no way of being told apart from "the
  // watch never managed to send anything".
  APP_LOG(APP_LOG_LEVEL_INFO, "History sent to phone: %d measurements", s_history_count);
}

static void prv_outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason,
                                      void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "History send failed, AppMessageResult %d", (int)reason);

  // Almost always "the phone was not connected yet at launch", so one retry a
  // few seconds later is worth it. Beyond that, the next time the app is opened
  // will do - there is nothing time-critical here.
  if (s_send_retried || s_send_retry_timer) {
    return;
  }
  s_send_retried = true;
  s_send_retry_timer = app_timer_register(SEND_RETRY_DELAY_MS, prv_send_retry, NULL);
}

// ------------------------------------------------------------- history screen

static uint16_t prv_menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index,
                                      void *data) {
  return s_history_count;
}

static int16_t prv_menu_get_header_height(MenuLayer *menu_layer, uint16_t section_index,
                                          void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_menu_draw_header(GContext *ctx, const Layer *cell_layer,
                                 uint16_t section_index, void *data) {
  static char header[24];
  snprintf(header, sizeof(header), "Last %d measurements", s_history_count);
  menu_cell_basic_header_draw(ctx, cell_layer, header);
}

static void prv_menu_draw_row(GContext *ctx, const Layer *cell_layer,
                              MenuIndex *cell_index, void *data) {
  // Newest first: storage keeps the newest last, the list shows it at the top.
  int index = s_history_count - 1 - cell_index->row;
  if (index < 0 || index >= s_history_count) {
    return;
  }
  const HrvRecord *record = &s_history[index];

  static char title[24];
  static char subtitle[32];

  snprintf(title, sizeof(title), "%u ms", (unsigned)record->rmssd_ms);

  time_t when = (time_t)record->timestamp;
  struct tm *local = localtime(&when);
  // Follows the watch's own 12/24-hour setting, like the rest of this repo's apps.
  strftime(subtitle, sizeof(subtitle),
           clock_is_24h_style() ? "%a %d %b, %H:%M" : "%a %d %b, %I:%M %p", local);

  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void prv_history_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);

  if (s_history_count == 0) {
    s_history_empty_layer = text_layer_create(GRect(8, 50, bounds.size.w - 16, 100));
    text_layer_set_text(s_history_empty_layer,
                        "Nothing measured yet.\n\nA measurement is taken when the watch "
                        "detects restful sleep.");
    text_layer_set_font(s_history_empty_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text_alignment(s_history_empty_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_history_empty_layer, GColorClear);
    layer_add_child(root_layer, text_layer_get_layer(s_history_empty_layer));
    return;
  }

  s_history_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_history_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = prv_menu_get_num_rows,
    .get_header_height = prv_menu_get_header_height,
    .draw_header = prv_menu_draw_header,
    .draw_row = prv_menu_draw_row,
  });
  menu_layer_set_click_config_onto_window(s_history_menu, window);
  layer_add_child(root_layer, menu_layer_get_layer(s_history_menu));
}

static void prv_history_window_unload(Window *window) {
  if (s_history_menu) {
    menu_layer_destroy(s_history_menu);
    s_history_menu = NULL;
  }
  if (s_history_empty_layer) {
    text_layer_destroy(s_history_empty_layer);
    s_history_empty_layer = NULL;
  }
  window_destroy(s_history_window);
  s_history_window = NULL;
}

static void prv_show_history(void) {
  // Re-read on every open: the worker may have added a measurement while this
  // app sat on the switch screen.
  prv_load_history();

  s_history_window = window_create();
  window_set_window_handlers(s_history_window, (WindowHandlers) {
    .load = prv_history_window_load,
    .unload = prv_history_window_unload,
  });
  window_stack_push(s_history_window, true);
}

// ---------------------------------------------------------- diagnostics screen

// Absolute rather than relative ("3h ago"), because the useful question in the
// morning is which part of the night something happened in.
static void prv_format_when(char *buffer, size_t size, uint32_t timestamp) {
  if (timestamp == 0) {
    strncpy(buffer, "never", size);
    buffer[size - 1] = '\0';
    return;
  }
  time_t when = (time_t)timestamp;
  struct tm *local = localtime(&when);
  strftime(buffer, size, clock_is_24h_style() ? "%a %H:%M" : "%a %I:%M %p", local);
}

static const char *prv_outcome_text(uint8_t outcome) {
  switch (outcome) {
    case HRV_OUTCOME_LOGGED:   return "recorded";
    case HRV_OUTCOME_TOO_FEW:  return "too few readings";
    case HRV_OUTCOME_OFF_WRIST: return "not on wrist";
    case HRV_OUTCOME_DISABLED: return "switched off";
    default:                   return "none yet";
  }
}

// Reads the worker's own account of the night. Every line is here to separate
// one failure from another: a worker that was not running, sleep that was never
// detected, restful sleep that never registered, a sensor that delivered
// nothing, or readings too sparse to compute an RMSSD from.
static void prv_build_diag_text(void) {
  HrvDiagnostics diag;
  if (!prv_load_diagnostics(&diag)) {
    snprintf(s_diag_text, sizeof(s_diag_text),
             "The background worker has not reported anything yet.\n\n"
             "Turn measuring on and leave it running; this screen fills in as "
             "the worker sees things.");
    return;
  }

  char started[16], tick[16], sleep_seen[16], restful_seen[16], outcome_at[16];
  prv_format_when(started, sizeof(started), diag.worker_started_at);
  prv_format_when(tick, sizeof(tick), diag.last_tick_at);
  prv_format_when(sleep_seen, sizeof(sleep_seen), diag.sleep_last_seen_at);
  prv_format_when(restful_seen, sizeof(restful_seen), diag.restful_last_seen_at);
  prv_format_when(outcome_at, sizeof(outcome_at), diag.last_outcome_at);

  snprintf(s_diag_text, sizeof(s_diag_text),
           "WORKER\n"
           "Started: %s\n"
           "Last alive: %s\n"
           "\n"
           "SLEEP DETECTION\n"
           "Asleep: %s\n"
           "Restful: %s\n"
           "Sleep events: %u\n"
           "\n"
           "SENSOR\n"
           "HRV readings: %u\n"
           "Off-wrist: %u\n"
           "Period granted: %s\n"
           "\n"
           "MEASUREMENTS\n"
           "Episodes: %u\n"
           "Last result: %s\n"
           "Last at: %s\n"
           "Readings used: %u",
           started, tick,
           sleep_seen, restful_seen, (unsigned)diag.sleep_events,
           (unsigned)diag.hrv_events, (unsigned)diag.hrv_zero_events,
           // Never requested is not the same as refused, and saying "no" here
           // would point at the sensor when nothing had asked it for anything.
           (diag.episodes == 0) ? "not requested" : (diag.hrv_request_ok ? "yes" : "no"),
           (unsigned)diag.episodes,
           prv_outcome_text(diag.last_outcome), outcome_at,
           (unsigned)diag.last_sample_count);
}

static void prv_diag_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);

  prv_build_diag_text();

  s_diag_scroll = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_diag_scroll, window);

  GRect text_bounds = GRect(6, 4, bounds.size.w - 12, 2000);
  s_diag_text_layer = text_layer_create(text_bounds);
  text_layer_set_text(s_diag_text_layer, s_diag_text);
  text_layer_set_font(s_diag_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_background_color(s_diag_text_layer, GColorClear);

  GSize used = text_layer_get_content_size(s_diag_text_layer);
  text_layer_set_size(s_diag_text_layer, GSize(text_bounds.size.w, used.h + 8));
  scroll_layer_set_content_size(s_diag_scroll, GSize(bounds.size.w, used.h + 16));

  scroll_layer_add_child(s_diag_scroll, text_layer_get_layer(s_diag_text_layer));
  layer_add_child(root_layer, scroll_layer_get_layer(s_diag_scroll));
}

static void prv_diag_window_unload(Window *window) {
  text_layer_destroy(s_diag_text_layer);
  scroll_layer_destroy(s_diag_scroll);
  window_destroy(s_diag_window);
  s_diag_window = NULL;
}

static void prv_show_diagnostics(void) {
  s_diag_window = window_create();
  window_set_window_handlers(s_diag_window, (WindowHandlers) {
    .load = prv_diag_window_load,
    .unload = prv_diag_window_unload,
  });
  window_stack_push(s_diag_window, true);
}

// --------------------------------------------------------------- switch screen

static void prv_update_display(void) {
  text_layer_set_text(s_state_layer, s_enabled ? "ON" : "OFF");

  if (app_worker_is_running()) {
    snprintf(s_worker_text, sizeof(s_worker_text), "Background: running");
  } else if (s_enabled) {
    // Enabled but not running is a real state, not a display glitch: the user
    // may still be looking at the system's "replace background app?" prompt, or
    // may have declined it.
    snprintf(s_worker_text, sizeof(s_worker_text), "Background: starting...");
  } else {
    snprintf(s_worker_text, sizeof(s_worker_text), "Background: stopped");
  }
  text_layer_set_text(s_worker_layer, s_worker_text);

  text_layer_set_text(s_hint_layer, s_enabled
      ? "SELECT to turn off\nDOWN history  UP status"
      : "SELECT to turn on\nDOWN history  UP status");
}

static void prv_poll_worker_state(void *data) {
  s_worker_poll_timer = NULL;
  prv_update_display();

  if (--s_worker_polls_left > 0) {
    s_worker_poll_timer = app_timer_register(WORKER_POLL_INTERVAL_MS, prv_poll_worker_state, NULL);
  }
}

static void prv_schedule_worker_poll(void) {
  if (s_worker_poll_timer) {
    app_timer_cancel(s_worker_poll_timer);
  }
  s_worker_polls_left = WORKER_POLL_ATTEMPTS;
  s_worker_poll_timer = app_timer_register(WORKER_POLL_INTERVAL_MS, prv_poll_worker_state, NULL);
}

// Brings the worker in line with the switch. The worker also reads the stored
// flag for itself, so this is about not occupying the watch's single background
// slot while measuring is off - not about correctness of the measurements.
static void prv_apply_worker_state(void) {
  if (s_enabled) {
    if (!app_worker_is_running()) {
      app_worker_launch();
    }
  } else if (app_worker_is_running()) {
    app_worker_kill();
  }
  prv_schedule_worker_poll();
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_enabled = !s_enabled;
  persist_write_bool(PERSIST_KEY_HRV_ENABLED, s_enabled);
  prv_apply_worker_state();
  prv_update_display();
  vibes_short_pulse();
}

static void prv_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_show_history();
}

static void prv_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_show_diagnostics();
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click_handler);
}

static void prv_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root_layer);

  s_title_layer = text_layer_create(GRect(0, 8, bounds.size.w, 30));
  text_layer_set_text(s_title_layer, "HRV Measuring");
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(root_layer, text_layer_get_layer(s_title_layer));

  s_state_layer = text_layer_create(GRect(0, 40, bounds.size.w, 50));
  text_layer_set_font(s_state_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_state_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_state_layer, GColorClear);
  layer_add_child(root_layer, text_layer_get_layer(s_state_layer));

  s_worker_layer = text_layer_create(GRect(0, 94, bounds.size.w, 22));
  text_layer_set_font(s_worker_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_worker_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_worker_layer, GColorClear);
  layer_add_child(root_layer, text_layer_get_layer(s_worker_layer));

  s_hint_layer = text_layer_create(GRect(6, 126, bounds.size.w - 12, bounds.size.h - 148));
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_hint_layer, GColorClear);
  layer_add_child(root_layer, text_layer_get_layer(s_hint_layer));

  // Shown so a bug report can name the build it came from.
  s_version_layer = text_layer_create(GRect(0, bounds.size.h - 22, bounds.size.w, 20));
  text_layer_set_text(s_version_layer, "v" APP_VERSION);
  text_layer_set_font(s_version_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_version_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_version_layer, GColorClear);
  layer_add_child(root_layer, text_layer_get_layer(s_version_layer));

  prv_update_display();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_version_layer);
  text_layer_destroy(s_hint_layer);
  text_layer_destroy(s_worker_layer);
  text_layer_destroy(s_state_layer);
  text_layer_destroy(s_title_layer);
}

static void prv_init(void) {
  s_enabled = prv_read_enabled();
  prv_load_history();

  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_register_outbox_sent(prv_outbox_sent_handler);
  app_message_register_outbox_failed(prv_outbox_failed_handler);
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);

  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  // Opening the app is also what installs the worker after a fresh install, and
  // what recovers it if another app's worker displaced ours.
  prv_apply_worker_state();

  // The history reaches the phone either when PebbleKit JS announces itself, or
  // from this timer if it never does. Opening this app is the only moment that
  // can happen at all, since the worker has no way to talk to the phone.
  s_initial_send_timer = app_timer_register(INITIAL_SEND_DELAY_MS, prv_initial_send, NULL);
}

static void prv_deinit(void) {
  if (s_worker_poll_timer) {
    app_timer_cancel(s_worker_poll_timer);
    s_worker_poll_timer = NULL;
  }
  if (s_send_retry_timer) {
    app_timer_cancel(s_send_retry_timer);
    s_send_retry_timer = NULL;
  }
  if (s_initial_send_timer) {
    app_timer_cancel(s_initial_send_timer);
    s_initial_send_timer = NULL;
  }
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
