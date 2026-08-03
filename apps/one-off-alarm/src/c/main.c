#include <pebble.h>

// Schedules one-off wake-up alarms at arbitrary points in the future (e.g. "in
// 3 days at 09:00"), using the Wakeup API so the watch relaunches this app and
// vibrates even if it was closed in the meantime. These are not recurring
// alarms - each fires once and is then removed. Several can be pending at the
// same time, up to the platform's ceiling of 8.

#define APP_VERSION "1.4"

// The SDK allows at most 8 wakeup events per app, so that is also the ceiling
// on pending alarms.
#define MAX_ALARMS 8

// Keys 100/101 are the pre-multi-alarm single-alarm format, read once at
// startup for migration and then deleted. 102 survives as the last-used
// vibration setting, which seeds the setup screen.
#define PERSIST_KEY_LEGACY_WAKEUP_ID 100
#define PERSIST_KEY_LEGACY_TARGET_TS 101
#define PERSIST_KEY_INTENSITY 102
#define PERSIST_KEY_ALARMS 200

#define MINUTE_STEP 5

// How long an alarm keeps vibrating before giving up. Without a cap it would
// buzz every 1-4 seconds until the battery died - a real failure mode for an
// app whose whole point is to still be working weeks from now.
#define VIBE_TIMEOUT_SEC 300

typedef enum { FIELD_DAYS, FIELD_HOUR, FIELD_MINUTE, FIELD_INTENSITY, NUM_FIELDS } SetupField;
typedef enum { VIBE_MILD, VIBE_MEDIUM, VIBE_AGGRESSIVE, NUM_VIBE_INTENSITIES } VibeIntensity;

static const char *const VIBE_INTENSITY_NAMES[NUM_VIBE_INTENSITIES] = {
  "Mild", "Medium", "Aggressive"
};
// How often the alarm re-vibrates while firing, per intensity.
static const uint32_t VIBE_INTENSITY_INTERVAL_MS[NUM_VIBE_INTENSITIES] = {
  4000, 2000, 1000
};

typedef struct {
  WakeupId id;
  time_t at;
  VibeIntensity intensity;
} Alarm;

// What actually goes to storage. The timestamp is deliberately absent: the OS
// knows it, and wakeup_query() is authoritative, so storage stays a cache that
// heals itself instead of a second copy that can drift.
typedef struct __attribute__((__packed__)) {
  int32_t id;
  uint8_t intensity;
} StoredAlarm;

// Kept sorted ascending, so s_alarms[0] is always the next one to ring - which
// is what both the list and the app glance want.
static Alarm s_alarms[MAX_ALARMS];
static int s_alarm_count;

static Window *s_list_window;
static MenuLayer *s_menu_layer;

static Window *s_setup_window;
static TextLayer *s_setup_title_layer;
static TextLayer *s_setup_value_layer;
static TextLayer *s_setup_hint_layer;

static Window *s_firing_window;
static TextLayer *s_firing_title_layer;
static TextLayer *s_firing_value_layer;
static TextLayer *s_firing_hint_layer;

static SetupField s_field = FIELD_DAYS;
static int s_days = 1;
static int s_hour = 9;
static int s_minute = 0;
static VibeIntensity s_intensity = VIBE_MEDIUM;

static AppTimer *s_vibe_timer;
static time_t s_fired_at;
static bool s_vibe_timed_out;
static bool s_firing_active;
static VibeIntensity s_firing_intensity = VIBE_MEDIUM;

static char s_menu_header_buf[32];
static char s_setup_title_buf[32];
static char s_setup_value_buf[160];
static char s_setup_hint_buf[96];
static char s_firing_title_buf[32];
static char s_firing_value_buf[64];
static char s_firing_hint_buf[64];

static void update_app_glance(void);

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

// Formats a clock time the way the watch itself is configured to show it.
// There's no SDK helper for this - clock_copy_time_string() only handles the
// current time - so the 12-hour case is built by hand. strftime's %I is no use
// because it pads to "09:00 AM", and %l isn't something to rely on.
static void format_clock_time(char *buf, size_t buf_size, time_t at) {
  struct tm *t = localtime(&at);
  if (clock_is_24h_style()) {
    strftime(buf, buf_size, "%H:%M", t);
    return;
  }
  // Both midnight and noon map to 12 - the classic off-by-twelve here.
  int hour12 = t->tm_hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  snprintf(buf, buf_size, "%d:%02d %s", hour12, t->tm_min,
           t->tm_hour < 12 ? "AM" : "PM");
}

// Same, but for the hour/minute the setup screen is still editing, where the
// two halves are separate fields rather than one timestamp.
static void format_picker_time(char *buf, size_t buf_size, int hour, int minute,
                               bool bracket_hour, bool bracket_minute) {
  char hour_buf[8];
  char minute_buf[8];

  if (clock_is_24h_style()) {
    snprintf(hour_buf, sizeof(hour_buf), "%02d", hour);
  } else {
    int hour12 = hour % 12;
    if (hour12 == 0) {
      hour12 = 12;
    }
    snprintf(hour_buf, sizeof(hour_buf), "%d", hour12);
  }
  snprintf(minute_buf, sizeof(minute_buf), "%02d", minute);

  // Brackets mark which half is being edited; the AM/PM suffix sits outside
  // them, since it isn't a field you can select.
  const char *suffix = clock_is_24h_style() ? "" : (hour < 12 ? " AM" : " PM");
  if (bracket_hour) {
    snprintf(buf, buf_size, "[%s]:%s%s", hour_buf, minute_buf, suffix);
  } else if (bracket_minute) {
    snprintf(buf, buf_size, "%s:[%s]%s", hour_buf, minute_buf, suffix);
  } else {
    snprintf(buf, buf_size, "%s:%s%s", hour_buf, minute_buf, suffix);
  }
}

// Splits a remaining duration into the coarsest sensible "in ..." phrasing.
static void format_remaining(char *buf, size_t buf_size, time_t at) {
  int secs_left = (int)(at - time(NULL));
  if (secs_left < 0) {
    secs_left = 0;
  }
  int d = secs_left / 86400;
  int h = (secs_left % 86400) / 3600;
  int m = (secs_left % 3600) / 60;

  if (d > 0) {
    snprintf(buf, buf_size, "in %dd %dh", d, h);
  } else if (h > 0) {
    snprintf(buf, buf_size, "in %dh %dm", h, m);
  } else if (secs_left > 0) {
    snprintf(buf, buf_size, "in %dm", m);
  } else {
    snprintf(buf, buf_size, "now");
  }
}

// --- Alarm storage ---------------------------------------------------------

static void save_alarms(void) {
  if (s_alarm_count == 0) {
    persist_delete(PERSIST_KEY_ALARMS);
    return;
  }
  StoredAlarm stored[MAX_ALARMS];
  for (int i = 0; i < s_alarm_count; i++) {
    stored[i].id = (int32_t)s_alarms[i].id;
    stored[i].intensity = (uint8_t)s_alarms[i].intensity;
  }
  persist_write_data(PERSIST_KEY_ALARMS, stored, sizeof(StoredAlarm) * s_alarm_count);
}

static void sort_alarms(void) {
  for (int i = 1; i < s_alarm_count; i++) {
    Alarm key = s_alarms[i];
    int j = i - 1;
    while (j >= 0 && s_alarms[j].at > key.at) {
      s_alarms[j + 1] = s_alarms[j];
      j--;
    }
    s_alarms[j + 1] = key;
  }
}

// Carries a pending alarm from the old single-alarm format into the new one.
// Runs once: as soon as PERSIST_KEY_ALARMS exists there is nothing to migrate.
static void migrate_legacy_alarm(void) {
  if (persist_exists(PERSIST_KEY_ALARMS) || !persist_exists(PERSIST_KEY_LEGACY_WAKEUP_ID)) {
    return;
  }

  WakeupId id = (WakeupId)persist_read_int(PERSIST_KEY_LEGACY_WAKEUP_ID);
  time_t at;
  if (wakeup_query(id, &at)) {
    VibeIntensity intensity = VIBE_MEDIUM;
    if (persist_exists(PERSIST_KEY_INTENSITY)) {
      int32_t stored_intensity = persist_read_int(PERSIST_KEY_INTENSITY);
      if (stored_intensity >= 0 && stored_intensity < NUM_VIBE_INTENSITIES) {
        intensity = (VibeIntensity)stored_intensity;
      }
    }
    StoredAlarm record = { .id = (int32_t)id, .intensity = (uint8_t)intensity };
    persist_write_data(PERSIST_KEY_ALARMS, &record, sizeof(record));
  }

  persist_delete(PERSIST_KEY_LEGACY_WAKEUP_ID);
  persist_delete(PERSIST_KEY_LEGACY_TARGET_TS);
}

// Rebuilds the in-memory list from storage, dropping anything the OS no longer
// has scheduled. That silently clears alarms which already rang or were
// cancelled elsewhere, so the stored list can never accumulate ghosts.
static void load_alarms(void) {
  s_alarm_count = 0;

  StoredAlarm stored[MAX_ALARMS];
  int bytes = persist_read_data(PERSIST_KEY_ALARMS, stored, sizeof(stored));
  if (bytes > 0) {
    int count = bytes / (int)sizeof(StoredAlarm);
    if (count > MAX_ALARMS) {
      count = MAX_ALARMS;
    }
    for (int i = 0; i < count; i++) {
      time_t at;
      if (!wakeup_query((WakeupId)stored[i].id, &at)) {
        continue;
      }
      Alarm *alarm = &s_alarms[s_alarm_count++];
      alarm->id = (WakeupId)stored[i].id;
      alarm->at = at;
      // Validate before trusting it as an array index later on.
      alarm->intensity = (stored[i].intensity < NUM_VIBE_INTENSITIES)
                           ? (VibeIntensity)stored[i].intensity
                           : VIBE_MEDIUM;
    }
  }

  sort_alarms();
  save_alarms();
}

static void remove_alarm_at(int index) {
  for (int i = index; i < s_alarm_count - 1; i++) {
    s_alarms[i] = s_alarms[i + 1];
  }
  s_alarm_count--;
  save_alarms();
}

static void remove_alarm_by_id(WakeupId id) {
  for (int i = 0; i < s_alarm_count; i++) {
    if (s_alarms[i].id == id) {
      remove_alarm_at(i);
      return;
    }
  }
}

// --- App glance ------------------------------------------------------------

// Shows a live countdown to the *next* alarm under the app's name in the
// launcher - but only while one is actually pending. The slice expires at that
// alarm's own fire time, so it clears itself without this app running, and no
// slice is added at all when the list is empty.
#if !PBL_PLATFORM_APLITE
// The template string's documented maximum length is 150 bytes.
#define GLANCE_BUF_SIZE 150

// Plain, non-updating countdown. Only used if the template below is
// rejected - it goes stale as time passes, but that beats showing nothing.
static void format_plain_countdown(char *buf, size_t buf_size, time_t at) {
  char remaining[24];
  format_remaining(remaining, sizeof(remaining), at);
  if (strcmp(remaining, "now") == 0) {
    snprintf(buf, buf_size, "Alarm ringing now");
  } else {
    snprintf(buf, buf_size, "Alarm %s", remaining);
  }
}

static void glance_reload_callback(AppGlanceReloadSession *session, size_t limit, void *context) {
  if (limit < 1 || s_alarm_count == 0) {
    return;
  }
  // Sorted ascending, so entry 0 is the one that rings first.
  time_t at = s_alarms[0].at;

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
    (long)at);

  AppGlanceSlice slice = {
    .layout.icon = APP_GLANCE_SLICE_DEFAULT_ICON,
    .layout.subtitle_template_string = glance_buf,
    .expiration_time = at,
  };

  if (app_glance_add_slice(session, slice) == APP_GLANCE_RESULT_SUCCESS) {
    return;
  }

  // Safety net: if the template is rejected for any reason, fall back to the
  // plain string rather than leaving the glance empty.
  APP_LOG(APP_LOG_LEVEL_WARNING, "Template glance rejected, falling back to plain text");
  format_plain_countdown(glance_buf, sizeof(glance_buf), at);
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

// --- Vibration -------------------------------------------------------------

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

static void stop_vibration(void) {
  if (s_vibe_timer) {
    app_timer_cancel(s_vibe_timer);
    s_vibe_timer = NULL;
  }
}

// --- Firing window ---------------------------------------------------------

static void update_firing_display(void) {
  if (!s_firing_title_layer) {
    return;
  }
  // Both states show the time rather than saying anything about why the alarm
  // was set. These are one-off alarms for any occasion - a reminder on a
  // weekday afternoon as easily as a morning wake-up - so telling the user to
  // get up would often be simply wrong.
  char time_buf[16];
  format_clock_time(time_buf, sizeof(time_buf), s_fired_at);

  if (s_vibe_timed_out) {
    // Vibration gave up on its own. Say so, and say when it rang - leaving
    // "Press any button to stop" up while nothing is buzzing would just be
    // confusing, and the time is useful if you missed it.
    snprintf(s_firing_title_buf, sizeof(s_firing_title_buf), "Alarm Rang");
    snprintf(s_firing_value_buf, sizeof(s_firing_value_buf), "at %s", time_buf);
    snprintf(s_firing_hint_buf, sizeof(s_firing_hint_buf), "Press any button");
  } else {
    snprintf(s_firing_title_buf, sizeof(s_firing_title_buf), "ALARM");
    snprintf(s_firing_value_buf, sizeof(s_firing_value_buf), "%s", time_buf);
    snprintf(s_firing_hint_buf, sizeof(s_firing_hint_buf), "Press any button\nto stop");
  }
  text_layer_set_text(s_firing_title_layer, s_firing_title_buf);
  text_layer_set_text(s_firing_value_layer, s_firing_value_buf);
  text_layer_set_text(s_firing_hint_layer, s_firing_hint_buf);
}

static void vibe_timer_callback(void *data) {
  if (time(NULL) - s_fired_at >= VIBE_TIMEOUT_SEC) {
    // Stop rather than re-arming: an alarm nobody dismisses must not keep
    // draining the battery indefinitely.
    s_vibe_timer = NULL;
    s_vibe_timed_out = true;
    update_firing_display();
    return;
  }
  play_vibe(s_firing_intensity);
  s_vibe_timer = app_timer_register(VIBE_INTENSITY_INTERVAL_MS[s_firing_intensity],
                                    vibe_timer_callback, NULL);
}

static void start_firing(WakeupId id, int32_t cookie) {
  // The cookie carries intensity + 1, so a firing alarm knows how hard to
  // buzz without consulting storage at all. Cookie 0 means an alarm scheduled
  // by a build that predates this, so fall back to the stored default.
  s_firing_intensity = s_intensity;
  if (cookie >= 1 && cookie <= NUM_VIBE_INTENSITIES) {
    s_firing_intensity = (VibeIntensity)(cookie - 1);
  }

  // It has rung, so it is no longer pending.
  remove_alarm_by_id(id);

  stop_vibration();
  s_fired_at = time(NULL);
  s_vibe_timed_out = false;

  if (!s_firing_active) {
    s_firing_active = true;
    window_stack_push(s_firing_window, true);
  } else {
    update_firing_display();
  }

  play_vibe(s_firing_intensity);
  s_vibe_timer = app_timer_register(VIBE_INTENSITY_INTERVAL_MS[s_firing_intensity],
                                    vibe_timer_callback, NULL);
}

static void firing_dismiss_click_handler(ClickRecognizerRef recognizer, void *context) {
  stop_vibration();
  window_stack_remove(s_firing_window, true);
}

static void firing_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, firing_dismiss_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, firing_dismiss_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, firing_dismiss_click_handler);
}

static void firing_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_firing_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 36));
  text_layer_set_font(s_firing_title_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_firing_title_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_firing_title_layer));

  s_firing_value_layer = text_layer_create(GRect(0, 42, bounds.size.w, bounds.size.h - 78));
  text_layer_set_font(s_firing_value_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_firing_value_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_firing_value_layer));

  s_firing_hint_layer = text_layer_create(GRect(0, bounds.size.h - 36, bounds.size.w, 36));
  text_layer_set_font(s_firing_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_firing_hint_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_firing_hint_layer));

  update_firing_display();
}

static void firing_window_unload(Window *window) {
  text_layer_destroy(s_firing_title_layer);
  text_layer_destroy(s_firing_value_layer);
  text_layer_destroy(s_firing_hint_layer);
  s_firing_title_layer = NULL;
  s_firing_value_layer = NULL;
  s_firing_hint_layer = NULL;
  s_firing_active = false;
  stop_vibration();
}

// --- Setup window: pick "in N days at HH:MM", plus vibration intensity -----

static void update_setup_display(void) {
  time_t target = compute_target_timestamp();
  char date_buf[32];
  // Date only, and placed directly beneath the day row it belongs to: this
  // line exists to answer "which date is 'In 3 days', exactly?", so sitting
  // next to that row is what makes the connection obvious. The time is spelled
  // out on the "At" row and would push this past the screen width anyway once
  // it carries an AM/PM suffix.
  strftime(date_buf, sizeof(date_buf), "%a %d %b", localtime(&target));

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
  format_picker_time(time_buf, sizeof(time_buf), s_hour, s_minute,
                     s_field == FIELD_HOUR, s_field == FIELD_MINUTE);

  snprintf(s_setup_title_buf, sizeof(s_setup_title_buf), "New Alarm");
  snprintf(s_setup_value_buf, sizeof(s_setup_value_buf),
    "%c %s\n%s\n%c At  %s\n%c Vibe %s",
    s_field == FIELD_DAYS ? '>' : ' ', day_buf,
    date_buf,
    (s_field == FIELD_HOUR || s_field == FIELD_MINUTE) ? '>' : ' ', time_buf,
    s_field == FIELD_INTENSITY ? '>' : ' ', VIBE_INTENSITY_NAMES[s_intensity]);
  snprintf(s_setup_hint_buf, sizeof(s_setup_hint_buf),
    "UP/DN: value  SELECT: next\nHold SELECT: add alarm");

  text_layer_set_text(s_setup_title_layer, s_setup_title_buf);
  text_layer_set_text(s_setup_value_layer, s_setup_value_buf);
  text_layer_set_text(s_setup_hint_layer, s_setup_hint_buf);
}

static void show_setup_error(const char *message) {
  snprintf(s_setup_value_buf, sizeof(s_setup_value_buf), "%s", message);
  text_layer_set_text(s_setup_value_layer, s_setup_value_buf);
}

static void add_alarm(void) {
  if (s_alarm_count >= MAX_ALARMS) {
    show_setup_error("Alarm limit\nreached (8)");
    return;
  }

  time_t target = compute_target_timestamp();
  if (target < time(NULL) + 35) {
    // Wakeup events cannot be scheduled within 30 seconds of "now".
    show_setup_error("Pick a time\nin the future");
    return;
  }

  // The OS refuses a wakeup within a minute of one that already exists, and
  // this app can lose track of its own if storage and the OS registry fall out
  // of step (across a reinstall, say) - an orphan then holds its slot forever
  // while the app shows nothing. Clearing fixes that, but only when we own no
  // alarms: with a populated list this would wipe the lot.
  if (s_alarm_count == 0) {
    wakeup_cancel_all();
  }

  // The cookie carries the intensity so a firing alarm doesn't have to look it
  // up. Offset by one to keep 0 meaning "unknown", as older builds used it.
  WakeupId id = wakeup_schedule(target, (int32_t)s_intensity + 1, true);
  if (id < 0) {
    switch (id) {
      case E_RANGE:
        // Something already holds a wakeup within a minute of this time - one
        // of ours we just failed to clear, or another app's entirely.
        show_setup_error("Time slot taken.\nTry another time.");
        break;
      case E_INVALID_ARGUMENT:
        show_setup_error("Time is in\nthe past");
        break;
      case E_OUT_OF_RESOURCES:
        show_setup_error("Too many alarms\nscheduled");
        break;
      default: {
        char message[48];
        snprintf(message, sizeof(message), "Could not set\nalarm (err %d)", (int)id);
        show_setup_error(message);
        break;
      }
    }
    return;
  }

  s_alarms[s_alarm_count].id = id;
  s_alarms[s_alarm_count].at = target;
  s_alarms[s_alarm_count].intensity = s_intensity;
  s_alarm_count++;
  sort_alarms();
  save_alarms();

  // Remember the choice as the default for the next alarm.
  persist_write_int(PERSIST_KEY_INTENSITY, (int32_t)s_intensity);

  window_stack_remove(s_setup_window, true);
}

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
  update_setup_display();
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
  update_setup_display();
}

static void setup_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_field = (SetupField)((s_field + 1) % NUM_FIELDS);
  update_setup_display();
}

static void setup_select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  add_alarm();
}

static void setup_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, setup_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, setup_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, setup_select_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, setup_select_long_click_handler, NULL);
}

static void setup_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_setup_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 36));
  text_layer_set_font(s_setup_title_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_setup_title_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_setup_title_layer));

  s_setup_value_layer = text_layer_create(GRect(0, 42, bounds.size.w, bounds.size.h - 78));
  text_layer_set_font(s_setup_value_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_setup_value_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_setup_value_layer));

  s_setup_hint_layer = text_layer_create(GRect(0, bounds.size.h - 36, bounds.size.w, 36));
  text_layer_set_font(s_setup_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_setup_hint_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_setup_hint_layer));

  s_field = FIELD_DAYS;
  update_setup_display();
}

static void setup_window_unload(Window *window) {
  text_layer_destroy(s_setup_title_layer);
  text_layer_destroy(s_setup_value_layer);
  text_layer_destroy(s_setup_hint_layer);
  s_setup_title_layer = NULL;
  s_setup_value_layer = NULL;
  s_setup_hint_layer = NULL;
}

// --- List window: the pending alarms --------------------------------------

static uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // One row per alarm, plus the trailing "add" row - which doubles as the
  // empty state when there are no alarms at all.
  return s_alarm_count + 1;
}

static int16_t menu_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  // The menu fills the window, so the header is the only place left for the
  // delete hint - and it's only worth the space once there's something to
  // delete. The empty state shows the version instead.
  if (s_alarm_count > 0) {
    snprintf(s_menu_header_buf, sizeof(s_menu_header_buf), "Alarms - hold to delete");
  } else {
    snprintf(s_menu_header_buf, sizeof(s_menu_header_buf), "Alarms  v%s", APP_VERSION);
  }
  menu_cell_basic_header_draw(ctx, cell_layer, s_menu_header_buf);
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->row >= s_alarm_count) {
    menu_cell_basic_draw(ctx, cell_layer, "+ New alarm", NULL, NULL);
    return;
  }

  const Alarm *alarm = &s_alarms[cell_index->row];

  char clock_buf[16];
  format_clock_time(clock_buf, sizeof(clock_buf), alarm->at);

  // Prefer the weekday - it's genuinely useful for an alarm days out - and only
  // drop it if the row can't hold it. In 12-hour mode the AM/PM suffix pushes
  // the full form right up against the screen width, so rather than guessing
  // whether it fits, measure it.
  char date_buf[16];
  strftime(date_buf, sizeof(date_buf), "%a %d %b", localtime(&alarm->at));

  char title[40];
  snprintf(title, sizeof(title), "%s %s", date_buf, clock_buf);

  GRect cell_bounds = layer_get_bounds(cell_layer);
  // Measure in a deliberately oversized box: constrained to the cell's own
  // width, TrailingEllipsis would truncate and the result could never exceed
  // it, which tells us nothing.
  GSize natural = graphics_text_layout_get_content_size(
      title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(0, 0, 1000, cell_bounds.size.h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

  // Covers the padding menu_cell_basic_draw applies itself, which isn't
  // documented - a conservative guess, tuned against the real display.
  const int16_t cell_text_inset = 10;
  if (natural.w > cell_bounds.size.w - cell_text_inset) {
    strftime(date_buf, sizeof(date_buf), "%d %b", localtime(&alarm->at));
    snprintf(title, sizeof(title), "%s %s", date_buf, clock_buf);
  }

  char remaining[24];
  format_remaining(remaining, sizeof(remaining), alarm->at);

  char subtitle[48];
  snprintf(subtitle, sizeof(subtitle), "%s - %s", remaining,
           VIBE_INTENSITY_NAMES[alarm->intensity]);

  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->row >= s_alarm_count) {
    window_stack_push(s_setup_window, true);
  }
  // Alarm rows have no short-press action; deleting deliberately needs a hold
  // so a stray tap can't discard one.
}

static void menu_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->row >= s_alarm_count) {
    return;
  }
  wakeup_cancel(s_alarms[cell_index->row].id);
  remove_alarm_at(cell_index->row);
  vibes_short_pulse();
  menu_layer_reload_data(s_menu_layer);
  update_app_glance();
}

static void list_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_get_num_rows,
    .get_header_height = menu_get_header_height,
    .draw_header = menu_draw_header,
    .draw_row = menu_draw_row,
    .select_click = menu_select_click,
    .select_long_click = menu_select_long_click,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void list_window_appear(Window *window) {
  // Coming back from adding, deleting or dismissing an alarm - redraw so the
  // countdowns and the glance both reflect the current list.
  menu_layer_reload_data(s_menu_layer);
  update_app_glance();
}

static void list_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

// --- Wakeup plumbing -------------------------------------------------------

// A wakeup can also fire while this app is already running (e.g. left open on
// the list) - the system delivers it here instead of relaunching the app, so
// this is required in addition to the launch_reason() check in init().
static void wakeup_handler(WakeupId id, int32_t cookie) {
  start_firing(id, cookie);
}

static void init(void) {
  migrate_legacy_alarm();
  load_alarms();

  if (persist_exists(PERSIST_KEY_INTENSITY)) {
    // Validate before trusting it as an array index - an unexpected value
    // would otherwise be an out-of-bounds read, and %s on a garbage pointer
    // crashes the app. Out of range keeps the VIBE_MEDIUM default.
    int32_t stored = persist_read_int(PERSIST_KEY_INTENSITY);
    if (stored >= 0 && stored < NUM_VIBE_INTENSITIES) {
      s_intensity = (VibeIntensity)stored;
    }
  }

  s_list_window = window_create();
  window_set_window_handlers(s_list_window, (WindowHandlers) {
    .load = list_window_load,
    .appear = list_window_appear,
    .unload = list_window_unload,
  });

  s_setup_window = window_create();
  window_set_window_handlers(s_setup_window, (WindowHandlers) {
    .load = setup_window_load,
    .unload = setup_window_unload,
  });
  window_set_click_config_provider(s_setup_window, setup_click_config_provider);

  s_firing_window = window_create();
  window_set_window_handlers(s_firing_window, (WindowHandlers) {
    .load = firing_window_load,
    .unload = firing_window_unload,
  });
  window_set_click_config_provider(s_firing_window, firing_click_config_provider);

  window_stack_push(s_list_window, true);

  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    WakeupId id = 0;
    int32_t cookie = 0;
    if (wakeup_get_launch_event(&id, &cookie)) {
      start_firing(id, cookie);
    }
  }

  wakeup_service_subscribe(wakeup_handler);
}

static void deinit(void) {
  stop_vibration();
  window_destroy(s_firing_window);
  window_destroy(s_setup_window);
  window_destroy(s_list_window);

  // Per Pebble's own App Glance example: do this last, right before exiting.
  update_app_glance();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
