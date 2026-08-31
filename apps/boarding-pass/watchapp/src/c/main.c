#include <pebble.h>

#include "code_matrix.h"

// Displays a boarding pass barcode as a scannable 2D symbol.
//
// The watch never encodes anything, and never learns which symbology it is
// drawing: the Android companion app reads the barcode out of a screenshot of
// the airline app, re-encodes it with ZXing and sends the finished module
// matrix here as a packed bit array. Aztec, QR and Data Matrix all arrive as
// the same square grid. All this app does is unpack those bits and paint them
// as black squares - which is exactly the part that has to keep working when
// the phone is in a bag at the gate, so the last matrix received is kept in
// persistent storage and drawn at startup without needing a phone at all.

// AppMessage keys. Protocol version 1; the phone app sends MSG_KEY_VERSION so
// a newer phone app talking to an older watchapp fails loudly instead of
// drawing garbage.
#define MSG_KEY_MODULES 1
#define MSG_KEY_MATRIX  2
#define MSG_KEY_LABEL   3
#define MSG_KEY_VERSION 4
#define MSG_KEY_CLEAR   5

#define PROTOCOL_VERSION 1

#define PERSIST_KEY_MATRIX    1
#define PERSIST_KEY_MODULES   2
#define PERSIST_KEY_LABEL     3
#define PERSIST_KEY_BACKLIGHT 4

// The symbol size limits, the packed bit layout and the on-screen geometry all
// live in code_matrix.h, shared with the host-side round-trip test.
#define MAX_LABEL_BYTES 32

// How long a transient message (backlight toggled, bad message received)
// replaces the label at the bottom of the screen.
#define FLASH_MS 1500

#define DEFAULT_STATUS "No boarding pass yet\n\nSend one from the phone app"


static Window *s_window;
static Layer *s_code_layer;
static TextLayer *s_label_layer;
static TextLayer *s_status_layer;

// The matrix, packed row by row, MSB first - the same layout the phone app
// produces. s_modules is 0 when nothing usable is stored.
static uint8_t s_matrix[CODE_MAX_MATRIX_BYTES];
static int s_modules;
static char s_label[MAX_LABEL_BYTES];

static char s_status[96];
static char s_flash[MAX_LABEL_BYTES];
static AppTimer *s_flash_timer;

static bool s_backlight = true;

// ---------------------------------------------------------------- storage --

static void load_stored_pass(void) {
  s_modules = 0;
  s_label[0] = '\0';

  if (!persist_exists(PERSIST_KEY_MODULES) || !persist_exists(PERSIST_KEY_MATRIX)) {
    return;
  }

  const int32_t modules = persist_read_int(PERSIST_KEY_MODULES);
  if (modules < CODE_MIN_MODULES || modules > CODE_MAX_MODULES) {
    return;
  }

  const int read = persist_read_data(PERSIST_KEY_MATRIX, s_matrix, sizeof(s_matrix));
  if (read != (int)code_matrix_bytes(modules)) {
    return;
  }

  s_modules = modules;
  if (persist_exists(PERSIST_KEY_LABEL)) {
    persist_read_string(PERSIST_KEY_LABEL, s_label, sizeof(s_label));
  }
}

// Deletes the stored pass, on request from the phone. A boarding pass that has
// been deleted on the phone should not outlive it on the wrist.
static void forget_pass(void) {
  persist_delete(PERSIST_KEY_MATRIX);
  persist_delete(PERSIST_KEY_MODULES);
  persist_delete(PERSIST_KEY_LABEL);
  s_modules = 0;
  s_label[0] = '\0';
  s_flash[0] = '\0';
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }
  snprintf(s_status, sizeof(s_status), "%s", DEFAULT_STATUS);
}

static void store_pass(void) {
  // The matrix goes first, and its module count only once it is safely down.
  // Storage can be full, and a matrix whose length no longer matches its
  // recorded size would just be thrown away at the next start anyway - better
  // to leave nothing than to leave half of something.
  const size_t wanted = code_matrix_bytes(s_modules);
  const int written = persist_write_data(PERSIST_KEY_MATRIX, s_matrix, wanted);
  if (written != (int)wanted) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "could not store the pass: %d", written);
    persist_delete(PERSIST_KEY_MATRIX);
    persist_delete(PERSIST_KEY_MODULES);
    persist_delete(PERSIST_KEY_LABEL);
    return;
  }
  persist_write_int(PERSIST_KEY_MODULES, s_modules);
  persist_write_string(PERSIST_KEY_LABEL, s_label);
}

// ---------------------------------------------------------------- drawing --

static void code_layer_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);

  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_modules <= 0) {
    return;
  }

  const int px = code_pixels_per_module(bounds.size.w, s_modules);
  const int size = px * s_modules;
  const int ox = bounds.origin.x + (bounds.size.w - size) / 2;
  const int oy = bounds.origin.y + (bounds.size.h - size) / 2;

  graphics_context_set_fill_color(ctx, GColorBlack);
  for (int row = 0; row < s_modules; row++) {
    int col = 0, run_start = 0, run_length = 0;
    while (code_next_run(s_matrix, s_modules, row, &col, &run_start, &run_length)) {
      graphics_fill_rect(ctx,
                         GRect(ox + run_start * px, oy + row * px, run_length * px, px),
                         0, GCornerNone);
    }
  }
}

static void flash_timer_callback(void *data) {
  s_flash_timer = NULL;
  s_flash[0] = '\0';
  text_layer_set_text(s_label_layer, s_label);
}

// Briefly shows a message where the label normally sits, for feedback that does
// not need a screen of its own.
static void flash(const char *message) {
  snprintf(s_flash, sizeof(s_flash), "%s", message);
  text_layer_set_text(s_label_layer, s_flash);
  if (s_flash_timer) {
    app_timer_reschedule(s_flash_timer, FLASH_MS);
  } else {
    s_flash_timer = app_timer_register(FLASH_MS, flash_timer_callback, NULL);
  }
}

static void update_ui(void) {
  const bool have_pass = s_modules > 0;

  layer_set_hidden(text_layer_get_layer(s_status_layer), have_pass);
  layer_set_hidden(s_code_layer, !have_pass);
  layer_set_hidden(text_layer_get_layer(s_label_layer), !have_pass);

  if (have_pass) {
    text_layer_set_text(s_label_layer, s_flash[0] ? s_flash : s_label);
    layer_mark_dirty(s_code_layer);
  } else {
    text_layer_set_text(s_status_layer, s_status);
  }
}

// A bad message must not throw away a boarding pass that is already on screen -
// the user may be standing at the gate with it. So problems are reported next
// to a good code and only take over the screen when there is nothing to lose.
static void report(const char *message) {
  if (s_modules > 0) {
    flash(message);
  } else {
    snprintf(s_status, sizeof(s_status), "%s", message);
    update_ui();
  }
}

// ------------------------------------------------------------ app message --

// Numbers survive the trip from Android as whatever width the Pebble app chose,
// so read the value out by length rather than assuming uint8.
static bool tuple_int(const Tuple *tuple, int32_t *out) {
  if (!tuple) {
    return false;
  }
  if (tuple->type == TUPLE_UINT) {
    switch (tuple->length) {
      case 1: *out = tuple->value->uint8; return true;
      case 2: *out = tuple->value->uint16; return true;
      case 4: *out = (int32_t)tuple->value->uint32; return true;
      default: return false;
    }
  }
  if (tuple->type == TUPLE_INT) {
    switch (tuple->length) {
      case 1: *out = tuple->value->int8; return true;
      case 2: *out = tuple->value->int16; return true;
      case 4: *out = tuple->value->int32; return true;
      default: return false;
    }
  }
  return false;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  int32_t version = 0;
  if (!tuple_int(dict_find(iter, MSG_KEY_VERSION), &version) || version != PROTOCOL_VERSION) {
    report("Update the phone app");
    return;
  }

  int32_t clear = 0;
  if (tuple_int(dict_find(iter, MSG_KEY_CLEAR), &clear) && clear != 0) {
    forget_pass();
    update_ui();
    return;
  }

  int32_t modules = 0;
  if (!tuple_int(dict_find(iter, MSG_KEY_MODULES), &modules) ||
      modules < CODE_MIN_MODULES || modules > CODE_MAX_MODULES) {
    report("Code size unusable");
    return;
  }

  const Tuple *matrix = dict_find(iter, MSG_KEY_MATRIX);
  if (!matrix || matrix->type != TUPLE_BYTE_ARRAY ||
      matrix->length != code_matrix_bytes(modules)) {
    report("Code data damaged");
    return;
  }

  memcpy(s_matrix, matrix->value->data, matrix->length);
  s_modules = modules;

  // Copy by length rather than with %s: the terminator is the sender's promise,
  // and this message comes from outside the watch's address space.
  const Tuple *label = dict_find(iter, MSG_KEY_LABEL);
  s_label[0] = '\0';
  if (label && label->type == TUPLE_CSTRING && label->length > 0) {
    const size_t copied = label->length < sizeof(s_label) ? label->length
                                                          : sizeof(s_label) - 1;
    memcpy(s_label, label->value->cstring, copied);
    s_label[copied] = '\0';
  }

  s_flash[0] = '\0';
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }

  store_pass();
  update_ui();
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox message dropped: %d", (int)reason);
  report("Message dropped");
}

// ---------------------------------------------------------------- buttons --

// A reflective screen sometimes scans better with the backlight off under
// strong light, so the user gets to decide rather than the app.
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  s_backlight = !s_backlight;
  persist_write_bool(PERSIST_KEY_BACKLIGHT, s_backlight);
  light_enable(s_backlight);
  flash(s_backlight ? "Light on" : "Light off");
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// ----------------------------------------------------------------- window --

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  const int label_height = 26;
  const int code_height = bounds.size.h - label_height;

  s_code_layer = layer_create(GRect(0, 0, bounds.size.w, code_height));
  layer_set_update_proc(s_code_layer, code_layer_update_proc);
  layer_add_child(root, s_code_layer);

  s_label_layer = text_layer_create(GRect(0, code_height, bounds.size.w, label_height));
  text_layer_set_background_color(s_label_layer, GColorWhite);
  text_layer_set_text_color(s_label_layer, GColorBlack);
  text_layer_set_font(s_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_label_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_label_layer));

  s_status_layer = text_layer_create(GRect(8, 40, bounds.size.w - 16, bounds.size.h - 60));
  text_layer_set_background_color(s_status_layer, GColorWhite);
  text_layer_set_text_color(s_status_layer, GColorBlack);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  light_enable(s_backlight);
  update_ui();
}

static void window_unload(Window *window) {
  light_enable(false);
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }
  text_layer_destroy(s_status_layer);
  text_layer_destroy(s_label_layer);
  layer_destroy(s_code_layer);
}

static void init(void) {
  s_backlight = persist_exists(PERSIST_KEY_BACKLIGHT) ? persist_read_bool(PERSIST_KEY_BACKLIGHT) : true;
  snprintf(s_status, sizeof(s_status), "%s", DEFAULT_STATUS);
  load_stored_pass();

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  const AppMessageResult opened = app_message_open(512, 128);
  if (opened != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open failed: %d", (int)opened);
  }

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  light_enable(false);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
