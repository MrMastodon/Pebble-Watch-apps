#include <pebble.h>

// Schedules ONE wake-up alarm for an arbitrary point in the future (e.g. "in
// 1 day at 09:00"), using the Wakeup API so the watch relaunches this app
// and vibrates even if it was closed in the meantime. This is not a
// recurring daily alarm - it fires once, then the alarm is cleared.

#define ALARM_COOKIE 0
#define PERSIST_KEY_WAKEUP_ID 100
#define PERSIST_KEY_TARGET_TS 101
#define PERSIST_KEY_INTENSITY 102
#define MINUTE_STEP 5

typedef enum { MODE_SETUP, MODE_ALARM_SET, MODE_FIRING } AppMode;
typedef enum { FIELD_DAYS, FIELD_HOUR, FIELD_MINUTE, FIELD_INTENSITY, NUM_FIELDS } SetupField;
typedef enum { VIBE_MILD, VIBE_MEDIUM, VIBE_AGGRESSIVE, NUM_VIBE_INTENSITIES } VibeIntensity;

static const char *const VIBE_INTENSITY_NAMES[NUM_VIBE_INTENSITIES] = {
  "Mild", "Medium", "Aggressive"
};
// How often the alarm re-vibrates while firing, per intensity.
static const uint32_t VIBE_INTENSITY_INTERVAL_MS[NUM_VIBE_INTENSITIES] = {
  4000, 2000, 1000
};

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_value_layer;
static TextLayer *s_hint_layer;

static AppMode s_mode;
static SetupField s_field = FIELD_DAYS;
static int s_days = 1;
static int s_hour = 9;
static int s_minute = 0;
static VibeIntensity s_intensity = VIBE_MEDIUM;

static AppTimer *s_vibe_timer;

static char s_title_buf[32];
static char s_value_buf[160];
static char s_hint_buf[96];

static void setup_click_config_provider(void *context);
static void alarm_set_click_config_provider(void *context);
static void firing_click_config_provider(void *context);

static int wrap(int value, int lo, int hi_exclusive) {
  int range = hi_exclusive - lo;
  int v = (value - lo) % range;
  if (v < 0) {
    v += range;
  }
  return v + lo;
}

static time_t compute_target_timestamp(void) {
  time_t now = time(NULL);
  struct tm target_tm = *localtime(&now);
  target_tm.tm_mday += s_days;
  target_tm.tm_hour = s_hour;
  target_tm.tm_min = s_minute;
  target_tm.tm_sec = 0;
  return mktime(&target_tm);
}

static void update_display(void) {
  switch (s_mode) {
    case MODE_SETUP: {
      snprintf(s_title_buf, sizeof(s_title_buf), "New Alarm");
      snprintf(s_value_buf, sizeof(s_value_buf),
        "%c Days  %d\n%c Hour  %02d\n%c Min   %02d\n%c Vibe  %s",
        s_field == FIELD_DAYS ? '>' : ' ', s_days,
        s_field == FIELD_HOUR ? '>' : ' ', s_hour,
        s_field == FIELD_MINUTE ? '>' : ' ', s_minute,
        s_field == FIELD_INTENSITY ? '>' : ' ', VIBE_INTENSITY_NAMES[s_intensity]);
      snprintf(s_hint_buf, sizeof(s_hint_buf),
        "UP/DN: value  SELECT: next\nHold SELECT: set alarm");
      break;
    }
    case MODE_ALARM_SET: {
      time_t ts = (time_t)persist_read_int(PERSIST_KEY_TARGET_TS);
      char date_buf[32];
      strftime(date_buf, sizeof(date_buf), "%a %d %b\n%H:%M", localtime(&ts));
      time_t now = time(NULL);
      int secs_left = (int)(ts - now);
      if (secs_left < 0) {
        secs_left = 0;
      }
      int d = secs_left / 86400;
      int h = (secs_left % 86400) / 3600;
      int m = (secs_left % 3600) / 60;
      snprintf(s_title_buf, sizeof(s_title_buf), "Alarm Set");
      snprintf(s_value_buf, sizeof(s_value_buf), "%s\nin %dd %dh %dm", date_buf, d, h, m);
      snprintf(s_hint_buf, sizeof(s_hint_buf), "SELECT: cancel alarm");
      break;
    }
    case MODE_FIRING: {
      snprintf(s_title_buf, sizeof(s_title_buf), "WAKE UP!");
      snprintf(s_value_buf, sizeof(s_value_buf), "Time to get up!");
      snprintf(s_hint_buf, sizeof(s_hint_buf), "Press any button\nto stop");
      break;
    }
  }
  text_layer_set_text(s_title_layer, s_title_buf);
  text_layer_set_text(s_value_layer, s_value_buf);
  text_layer_set_text(s_hint_layer, s_hint_buf);
}

static void apply_mode(void) {
  switch (s_mode) {
    case MODE_SETUP:
      window_set_click_config_provider(s_window, setup_click_config_provider);
      break;
    case MODE_ALARM_SET:
      window_set_click_config_provider(s_window, alarm_set_click_config_provider);
      break;
    case MODE_FIRING:
      window_set_click_config_provider(s_window, firing_click_config_provider);
      break;
  }
  update_display();
}

static void clear_persisted_alarm(void) {
  persist_delete(PERSIST_KEY_WAKEUP_ID);
  persist_delete(PERSIST_KEY_TARGET_TS);
}

static void schedule_alarm(void) {
  time_t target = compute_target_timestamp();
  time_t now = time(NULL);
  if (target < now + 35) {
    // Wakeup events cannot be scheduled within 30 seconds of "now".
    snprintf(s_value_buf, sizeof(s_value_buf), "Pick a time\nin the future");
    text_layer_set_text(s_value_layer, s_value_buf);
    return;
  }

  WakeupId id = wakeup_schedule(target, ALARM_COOKIE, true);
  if (id < 0) {
    snprintf(s_value_buf, sizeof(s_value_buf), "Could not set\nalarm (err %d)", (int)id);
    text_layer_set_text(s_value_layer, s_value_buf);
    return;
  }

  persist_write_int(PERSIST_KEY_WAKEUP_ID, (int)id);
  persist_write_int(PERSIST_KEY_TARGET_TS, (int)target);
  persist_write_int(PERSIST_KEY_INTENSITY, (int)s_intensity);
  s_mode = MODE_ALARM_SET;
  apply_mode();
}

static void cancel_alarm(void) {
  if (persist_exists(PERSIST_KEY_WAKEUP_ID)) {
    WakeupId id = (WakeupId)persist_read_int(PERSIST_KEY_WAKEUP_ID);
    wakeup_cancel(id);
  }
  clear_persisted_alarm();
  s_mode = MODE_SETUP;
  apply_mode();
}

static void play_vibe(VibeIntensity intensity) {
  switch (intensity) {
    case VIBE_MILD:
      vibes_short_pulse();
      break;
    case VIBE_MEDIUM:
      vibes_double_pulse();
      break;
    case VIBE_AGGRESSIVE: {
      static const uint32_t segments[] = { 150, 100, 150, 100, 150, 100, 300 };
      VibePattern pattern = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
      };
      vibes_enqueue_custom_pattern(pattern);
      break;
    }
    default:
      break;
  }
}

static void vibe_timer_callback(void *data) {
  play_vibe(s_intensity);
  s_vibe_timer = app_timer_register(VIBE_INTENSITY_INTERVAL_MS[s_intensity], vibe_timer_callback, NULL);
}

static void enter_firing_mode(void) {
  clear_persisted_alarm();
  s_mode = MODE_FIRING;
  apply_mode();
  play_vibe(s_intensity);
  s_vibe_timer = app_timer_register(VIBE_INTENSITY_INTERVAL_MS[s_intensity], vibe_timer_callback, NULL);
}

// A wakeup can also fire while this app is already running (e.g. left open
// on the countdown screen) - the system delivers it here instead of
// relaunching the app, so this is required in addition to the
// launch_reason() check in init() below.
static void wakeup_handler(WakeupId id, int32_t cookie) {
  if (cookie == ALARM_COOKIE) {
    enter_firing_mode();
  }
}

// --- Setup mode: pick "in N days at HH:MM", plus vibration intensity ---

static void setup_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  switch (s_field) {
    case FIELD_DAYS: s_days = wrap(s_days + 1, 0, 181); break;
    case FIELD_HOUR: s_hour = wrap(s_hour + 1, 0, 24); break;
    case FIELD_MINUTE: s_minute = wrap(s_minute + MINUTE_STEP, 0, 60); break;
    case FIELD_INTENSITY:
      s_intensity = (VibeIntensity)wrap(s_intensity + 1, 0, NUM_VIBE_INTENSITIES);
      play_vibe(s_intensity);
      break;
    default: break;
  }
  update_display();
}

static void setup_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  switch (s_field) {
    case FIELD_DAYS: s_days = wrap(s_days - 1, 0, 181); break;
    case FIELD_HOUR: s_hour = wrap(s_hour - 1, 0, 24); break;
    case FIELD_MINUTE: s_minute = wrap(s_minute - MINUTE_STEP, 0, 60); break;
    case FIELD_INTENSITY:
      s_intensity = (VibeIntensity)wrap(s_intensity - 1, 0, NUM_VIBE_INTENSITIES);
      play_vibe(s_intensity);
      break;
    default: break;
  }
  update_display();
}

static void setup_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_field = (SetupField)((s_field + 1) % NUM_FIELDS);
  update_display();
}

static void setup_select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  schedule_alarm();
}

static void setup_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, setup_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, setup_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, setup_select_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, setup_select_long_click_handler, NULL);
}

// --- Alarm-set mode: show countdown, allow cancelling ---

static void alarm_set_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_alarm();
}

static void alarm_set_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, alarm_set_select_click_handler);
}

// --- Firing mode: any button dismisses the alarm ---

static void firing_dismiss_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_vibe_timer) {
    app_timer_cancel(s_vibe_timer);
    s_vibe_timer = NULL;
  }
  s_mode = MODE_SETUP;
  apply_mode();
}

static void firing_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, firing_dismiss_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, firing_dismiss_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, firing_dismiss_click_handler);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 38));
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_title_layer));

  s_value_layer = text_layer_create(GRect(0, 44, bounds.size.w, bounds.size.h - 84));
  text_layer_set_font(s_value_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_value_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_value_layer));

  s_hint_layer = text_layer_create(GRect(0, bounds.size.h - 40, bounds.size.w, 40));
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_hint_layer));

  if (persist_exists(PERSIST_KEY_INTENSITY)) {
    s_intensity = (VibeIntensity)persist_read_int(PERSIST_KEY_INTENSITY);
  }

  if (persist_exists(PERSIST_KEY_WAKEUP_ID)) {
    WakeupId id = (WakeupId)persist_read_int(PERSIST_KEY_WAKEUP_ID);
    time_t ts;
    if (wakeup_query(id, &ts)) {
      s_mode = MODE_ALARM_SET;
    } else {
      clear_persisted_alarm();
      s_mode = MODE_SETUP;
    }
  } else {
    s_mode = MODE_SETUP;
  }
  apply_mode();
}

static void window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_value_layer);
  text_layer_destroy(s_hint_layer);
}

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    WakeupId id = 0;
    int32_t cookie = 0;
    wakeup_get_launch_event(&id, &cookie);
    if (cookie == ALARM_COOKIE) {
      enter_firing_mode();
    }
  }

  wakeup_service_subscribe(wakeup_handler);
}

static void deinit(void) {
  if (s_vibe_timer) {
    app_timer_cancel(s_vibe_timer);
    s_vibe_timer = NULL;
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
