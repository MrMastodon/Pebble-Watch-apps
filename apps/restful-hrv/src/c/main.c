#include <pebble.h>

#include "../common/hrv_common.h"

// The entire user interface for Restful HRV: one screen, one switch. All the
// actual work happens in the background worker (worker_src/c/worker.c), which
// runs whether or not this app is open. There is deliberately nothing here for
// browsing past measurements - those are logged straight to the phone, and a
// second copy on the watch would only be a second thing that can disagree.

#define APP_VERSION "1.0"

// How long after toggling to re-check whether the worker actually started or
// stopped. Both operations are asynchronous, and launching one can put a
// confirmation prompt in front of the user first, so the answer is not known
// when app_worker_launch() returns.
#define WORKER_POLL_INTERVAL_MS 500
#define WORKER_POLL_ATTEMPTS 8

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_state_layer;
static TextLayer *s_worker_layer;
static TextLayer *s_hint_layer;
static TextLayer *s_version_layer;

static bool s_enabled;

static AppTimer *s_worker_poll_timer;
static int s_worker_polls_left;

static char s_worker_text[32];

static bool prv_read_enabled(void) {
  if (!persist_exists(PERSIST_KEY_HRV_ENABLED)) {
    return true;
  }
  return persist_read_bool(PERSIST_KEY_HRV_ENABLED);
}

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
      ? "Measures HRV during restful sleep. SELECT to turn off."
      : "No measurements will be taken. SELECT to turn on.");
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

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
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

  s_hint_layer = text_layer_create(GRect(6, 120, bounds.size.w - 12, bounds.size.h - 140));
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
}

static void prv_deinit(void) {
  if (s_worker_poll_timer) {
    app_timer_cancel(s_worker_poll_timer);
    s_worker_poll_timer = NULL;
  }
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
