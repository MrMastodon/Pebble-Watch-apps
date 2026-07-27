#include <pebble.h>

// Schedules ONE wake-up alarm for an arbitrary point in the future (e.g. "in
// 1 day at 09:00"), using the Wakeup API so the watch relaunches this app
// and vibrates even if it was closed in the meantime. This is not a
// recurring daily alarm - it fires once, then the alarm is cleared.

#define APP_VERSION "1.2"
#define ALARM_COOKIE 0
#define PERSIST_KEY_WAKEUP_ID 100
#define PERSIST_KEY_TARGET_TS 101
#define PERSIST_KEY_INTENSITY 102
#define MINUTE_STEP 5
// How long the alarm keeps vibrating before giving up. Without a cap it would
// buzz every 1-4 seconds until the battery died - a real failure mode for an
// app whose whole point is to still be working weeks from now.
#define VIBE_TIMEOUT_SEC 300

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
static time_t s_fired_at;
static bool s_vibe_timed_out;

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
      time_t target = compute_target_timestamp();
      char date_buf[32];
      strftime(date_buf, sizeof(date_buf), "%a %d %b %H:%M", localtime(&target));

      // The day count is relative ("3 days from now") while the time is an
      // absolute wall clock ("at 09:00"). Spelling both out in plain words is
      // what makes that distinction visible - a uniform "Days/Hour/Min" list
      // reads as if you were dialling in one duration.
      char day_buf[16];
      if (s_days == 0) {
        snprintf(day_buf, sizeof(day_buf), "Today");
      } else if (s_days == 1) {
        snprintf(day_buf, sizeof(day_buf), "Tomorrow");
      } else {
        snprintf(day_buf, sizeof(day_buf), "In %d days", s_days);
      }

      // Hour and minute share one row, so the row-leading '>' can't say which
      // of the two is being edited - brackets around the active half do.
      char time_buf[16];
      if (s_field == FIELD_HOUR) {
        snprintf(time_buf, sizeof(time_buf), "[%02d]:%02d", s_hour, s_minute);
      } else if (s_field == FIELD_MINUTE) {
        snprintf(time_buf, sizeof(time_buf), "%02d:[%02d]", s_hour, s_minute);
      } else {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", s_hour, s_minute);
      }

      snprintf(s_title_buf, sizeof(s_title_buf), "New Alarm");
      snprintf(s_value_buf, sizeof(s_value_buf),
        "%c %s\n%c At  %s\n%c Vibe %s\n%s",
        s_field == FIELD_DAYS ? '>' : ' ', day_buf,
        (s_field == FIELD_HOUR || s_field == FIELD_MINUTE) ? '>' : ' ', time_buf,
        s_field == FIELD_INTENSITY ? '>' : ' ', VIBE_INTENSITY_NAMES[s_intensity],
        date_buf);
      snprintf(s_hint_buf, sizeof(s_hint_buf),
        "UP/DN: value  SELECT: next\nHold SELECT: set alarm");
      break;
    }
    case MODE_ALARM_SET: {
      if (!persist_exists(PERSIST_KEY_TARGET_TS)) {
        // Shouldn't happen - window_load refreshes this from the OS - but
        // reading a missing key yields 0, which would render as Jan 1970.
        snprintf(s_title_buf, sizeof(s_title_buf), "Alarm Set");
        snprintf(s_value_buf, sizeof(s_value_buf), "Alarm is\nscheduled");
        snprintf(s_hint_buf, sizeof(s_hint_buf), "Hold SELECT: cancel\nv%s", APP_VERSION);
        break;
      }
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
      snprintf(s_hint_buf, sizeof(s_hint_buf), "Hold SELECT: cancel\nv%s", APP_VERSION);
      break;
    }
    case MODE_FIRING: {
      if (s_vibe_timed_out) {
        // Vibration gave up on its own. Say so, and say when it rang - leaving
        // "Press any button to stop" up while nothing is buzzing would just be
        // confusing, and the time is useful if you slept through it.
        char rang_buf[16];
        strftime(rang_buf, sizeof(rang_buf), "%H:%M", localtime(&s_fired_at));
        snprintf(s_title_buf, sizeof(s_title_buf), "Alarm Rang");
        snprintf(s_value_buf, sizeof(s_value_buf), "at %s", rang_buf);
        snprintf(s_hint_buf, sizeof(s_hint_buf), "Press any button");
      } else {
        snprintf(s_title_buf, sizeof(s_title_buf), "WAKE UP!");
        snprintf(s_value_buf, sizeof(s_value_buf), "Time to get up!");
        snprintf(s_hint_buf, sizeof(s_hint_buf), "Press any button\nto stop");
      }
      break;
    }
  }
  text_layer_set_text(s_title_layer, s_title_buf);
  text_layer_set_text(s_value_layer, s_value_buf);
  text_layer_set_text(s_hint_layer, s_hint_buf);
}

// Shows a live countdown under the app's name in the launcher menu - but
// only while an alarm is actually set. It expires automatically at the
// alarm's own fire time, so it clears itself without the app needing to
// run again, and we explicitly clear it (add no slice) whenever there's no
// alarm, so nothing is ever shown outside of those two cases.
#if !PBL_PLATFORM_APLITE
// The template string's documented maximum length is 150 bytes.
#define GLANCE_BUF_SIZE 150

// Plain, non-updating countdown. Only used if the template below is
// rejected - it goes stale as time passes, but that beats showing nothing.
static void format_plain_countdown(char *buf, size_t buf_size, time_t ts) {
  int secs_left = (int)(ts - time(NULL));
  if (secs_left < 0) {
    secs_left = 0;
  }
  int d = secs_left / 86400;
  int h = (secs_left % 86400) / 3600;
  int m = (secs_left % 3600) / 60;

  if (d > 0) {
    snprintf(buf, buf_size, "Alarm in %dd %dh", d, h);
  } else if (h > 0) {
    snprintf(buf, buf_size, "Alarm in %dh %dm", h, m);
  } else if (secs_left > 0) {
    snprintf(buf, buf_size, "Alarm in %dm", m);
  } else {
    snprintf(buf, buf_size, "Alarm ringing now");
  }
}

static void glance_reload_callback(AppGlanceReloadSession *session, size_t limit, void *context) {
  if (limit < 1 || !persist_exists(PERSIST_KEY_TARGET_TS)) {
    return;
  }
  time_t ts = (time_t)persist_read_int(PERSIST_KEY_TARGET_TS);

  // Preferred form: a template the system re-evaluates each time the launcher
  // draws this row, so the countdown stays current without this app running.
  // Syntax per the AppGlance C API guide - note that each quoted time-format
  // holds exactly one %-specifier, and the parameters are comma-separated with
  // no spaces. An earlier attempt broke both of those and was rejected
  // silently, which is why nothing showed up at all.
  //
  // Each rung names its unit explicitly (%ad/%aH/%aM) rather than using the
  // auto format %aT, which drags seconds along - pointless churn on a glance
  // you only see in passing. Minutes are as fine as this needs to get; under a
  // minute the unpredicated fallback takes over.
  char glance_buf[GLANCE_BUF_SIZE];
  snprintf(glance_buf, sizeof(glance_buf),
    "Alarm {time_until(%ld)|format(>=1d:'%%ad left',>=1H:'%%aH left',>=1M:'%%aM left','now')}",
    (long)ts);

  AppGlanceSlice slice = {
    .layout.icon = APP_GLANCE_SLICE_DEFAULT_ICON,
    .layout.subtitle_template_string = glance_buf,
    .expiration_time = ts,
  };

  if (app_glance_add_slice(session, slice) == APP_GLANCE_RESULT_SUCCESS) {
    return;
  }

  // Safety net: if the template is rejected for any reason, fall back to the
  // plain string rather than leaving the glance empty.
  APP_LOG(APP_LOG_LEVEL_WARNING, "Template glance rejected, falling back to plain text");
  format_plain_countdown(glance_buf, sizeof(glance_buf), ts);
  slice.layout.subtitle_template_string = glance_buf;
  AppGlanceResult result = app_glance_add_slice(session, slice);
  if (result != APP_GLANCE_RESULT_SUCCESS) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "app_glance_add_slice() returned %d", result);
  }
}
#endif

static void update_app_glance(void) {
#if !PBL_PLATFORM_APLITE
  app_glance_reload(glance_reload_callback, NULL);
#endif
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
  update_app_glance();
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

  // The OS refuses a wakeup within a minute of one that already exists, and
  // this app can lose track of its own: it only knows about an alarm through
  // PERSIST_KEY_WAKEUP_ID, so if storage and the OS registry ever fall out of
  // step, an orphaned wakeup keeps occupying the slot while the app shows the
  // setup screen. Clearing first makes that self-healing. Safe because we only
  // get here from MODE_SETUP - i.e. when no alarm is supposed to exist - and
  // because this only cancels wakeups belonging to this app.
  wakeup_cancel_all();

  WakeupId id = wakeup_schedule(target, ALARM_COOKIE, true);
  if (id < 0) {
    switch (id) {
      case E_RANGE:
        // Another app holds a wakeup within a minute of this time; we can't
        // clear that one, so the user has to pick a different slot.
        snprintf(s_value_buf, sizeof(s_value_buf), "Time slot taken.\nTry another time.");
        break;
      case E_INVALID_ARGUMENT:
        snprintf(s_value_buf, sizeof(s_value_buf), "Time is in\nthe past");
        break;
      case E_OUT_OF_RESOURCES:
        snprintf(s_value_buf, sizeof(s_value_buf), "Too many alarms\nscheduled");
        break;
      default:
        snprintf(s_value_buf, sizeof(s_value_buf), "Could not set\nalarm (err %d)", (int)id);
        break;
    }
    text_layer_set_text(s_value_layer, s_value_buf);
    return;
  }

  // time_t is 32-bit here (the SDK builds with -Dtime_t=long), so this stores
  // the full value. The 2038 ceiling is the platform's, not ours.
  persist_write_int(PERSIST_KEY_WAKEUP_ID, (int32_t)id);
  persist_write_int(PERSIST_KEY_TARGET_TS, (int32_t)target);
  persist_write_int(PERSIST_KEY_INTENSITY, (int32_t)s_intensity);
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
  if (time(NULL) - s_fired_at >= VIBE_TIMEOUT_SEC) {
    // Stop rather than re-arming: an alarm nobody dismisses must not keep
    // draining the battery indefinitely.
    s_vibe_timer = NULL;
    s_vibe_timed_out = true;
    update_display();
    return;
  }
  play_vibe(s_intensity);
  s_vibe_timer = app_timer_register(VIBE_INTENSITY_INTERVAL_MS[s_intensity], vibe_timer_callback, NULL);
}

static void enter_firing_mode(void) {
  clear_persisted_alarm();
  // Defensive: never leave a previous timer running and unreachable.
  if (s_vibe_timer) {
    app_timer_cancel(s_vibe_timer);
    s_vibe_timer = NULL;
  }
  s_fired_at = time(NULL);
  s_vibe_timed_out = false;
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

static void alarm_set_select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  cancel_alarm();
}

static void alarm_set_click_config_provider(void *context) {
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, alarm_set_select_long_click_handler, NULL);
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

  s_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 36));
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_title_layer));

  s_value_layer = text_layer_create(GRect(0, 42, bounds.size.w, bounds.size.h - 78));
  text_layer_set_font(s_value_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_value_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_value_layer));

  s_hint_layer = text_layer_create(GRect(0, bounds.size.h - 36, bounds.size.w, 36));
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_hint_layer));

  if (persist_exists(PERSIST_KEY_INTENSITY)) {
    // Validate before trusting it as an array index - an unexpected value
    // would otherwise be an out-of-bounds read, and %s on a garbage pointer
    // crashes the app. Out of range keeps the VIBE_MEDIUM default.
    int32_t stored = persist_read_int(PERSIST_KEY_INTENSITY);
    if (stored >= 0 && stored < NUM_VIBE_INTENSITIES) {
      s_intensity = (VibeIntensity)stored;
    }
  }

  if (persist_exists(PERSIST_KEY_WAKEUP_ID)) {
    WakeupId id = (WakeupId)persist_read_int(PERSIST_KEY_WAKEUP_ID);
    time_t ts;
    if (wakeup_query(id, &ts)) {
      // The OS knows the real scheduled time, so treat it as authoritative and
      // refresh our stored copy from it rather than trusting a value that may
      // have drifted or gone missing.
      persist_write_int(PERSIST_KEY_TARGET_TS, (int32_t)ts);
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

  // Per Pebble's own App Glance example: do this last, right before exiting.
  update_app_glance();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
