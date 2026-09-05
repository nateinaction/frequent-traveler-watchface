// Frequent Traveller — a Pebble watchface for people who live across
// timezones. The body of the screen is a stack of full-width colour bands, one
// per timezone:
//
//   * your local time, always present, on a RED band
//   * UTC, always present, on a BLUE band
//   * up to MAX_ZONES extra zones you configure, on the plain background
//
// Bands are ordered east to west by UTC offset, so every zone ahead of UTC sits
// above the blue band and every zone behind it sits below. A configured zone
// that currently resolves to the local or UTC offset is hidden rather than
// drawn as a duplicate band.
//
// The background is white by default (a dark theme is available in Settings).
// A slim header across the top carries just the date — unlike the watchface
// this is modelled on, there is deliberately no time in the header, because
// local time already has a band of its own.
//
// Any row whose calendar date differs from your local date gets a "+1" / "-1"
// suffix on its label.
//
// The watch itself doesn't know about IANA timezones or DST. The phone config
// page resolves each zone's current UTC offset (in minutes) and ships it down
// over AppMessage; see src/pkjs/index.js for why that resolution can't happen
// in pkjs itself.

#include <pebble.h>

// MAX_ZONES is the user-configurable extra zones. Two more rows (local + UTC)
// are always drawn, so MAX_ROWS bounds every row loop in this file.
#define MAX_ZONES      6
#define MAX_ROWS       (MAX_ZONES + 2)
#define MAX_LABEL_LEN  12

#define PERSIST_LOCAL_OFFSET 1
#define PERSIST_NUM_ZONES    2
#define PERSIST_ZONES_BLOB   3
#define PERSIST_H24          4
#define PERSIST_DARK         5
#define PERSIST_LOCAL_LABEL  6

typedef struct {
  char    label[MAX_LABEL_LEN + 1];
  int32_t offset_min;               // minutes east of UTC
} Zone;

// AppMessage keys for per-slot zone values. A JS-side array fans out to
// individual keys because per-element tuples don't arrive reliably.
// MESSAGE_KEY_* are linker-time constants rather than compile-time ones, so
// these lookup tables are filled in at runtime by init_message_keys().
static uint32_t kZLabelKey[MAX_ZONES];
static uint32_t kZOffsetKey[MAX_ZONES];

static void init_message_keys(void) {
  kZLabelKey[0] = MESSAGE_KEY_Z_LABEL_0;
  kZLabelKey[1] = MESSAGE_KEY_Z_LABEL_1;
  kZLabelKey[2] = MESSAGE_KEY_Z_LABEL_2;
  kZLabelKey[3] = MESSAGE_KEY_Z_LABEL_3;
  kZLabelKey[4] = MESSAGE_KEY_Z_LABEL_4;
  kZLabelKey[5] = MESSAGE_KEY_Z_LABEL_5;
  kZOffsetKey[0] = MESSAGE_KEY_Z_OFFSET_0;
  kZOffsetKey[1] = MESSAGE_KEY_Z_OFFSET_1;
  kZOffsetKey[2] = MESSAGE_KEY_Z_OFFSET_2;
  kZOffsetKey[3] = MESSAGE_KEY_Z_OFFSET_3;
  kZOffsetKey[4] = MESSAGE_KEY_Z_OFFSET_4;
  kZOffsetKey[5] = MESSAGE_KEY_Z_OFFSET_5;
}

// ---- state ----
static Window *s_window;
static Layer  *s_header_layer;
static Layer  *s_rows_layer;

static Zone    s_zones[MAX_ZONES];
static int     s_num_zones        = 0;
static int32_t s_local_offset_min = 0;
static char    s_local_label[MAX_LABEL_LEN + 1] = "LOCAL";
static bool    s_h24              = true;
static bool    s_dark             = false;

// -----------------------------------------------------------------------------
// Theme
// -----------------------------------------------------------------------------
//
// Only the plain rows and the header follow the theme. The local (red) and UTC
// (blue) bands are fixed — they're the point of the watchface, and their white
// text reads well on both.

static GColor theme_bg(void)  { return s_dark ? GColorBlack : GColorWhite; }
static GColor theme_fg(void)  { return s_dark ? GColorWhite : GColorBlack; }
static GColor theme_rule(void) { return s_dark ? GColorDarkGray : GColorLightGray; }

// -----------------------------------------------------------------------------
// Row fonts
// -----------------------------------------------------------------------------
//
// Rows share the body evenly, so the font has to shrink as zones are added.
// line_h is the drawn height of the font (used to centre text in its band) and
// time_w is the column reserved for the right-hand clock, which is wider in
// 12-hour mode because of the trailing a/p.

typedef struct {
  const char *key;
  int         line_h;
  int         min_row_h;   // smallest band this font is allowed in
  int         time_w_24;
  int         time_w_12;
} RowFont;

static const RowFont ROW_FONTS[] = {
  { FONT_KEY_GOTHIC_28_BOLD, 30, 40, 76, 92 },
  { FONT_KEY_GOTHIC_24_BOLD, 26, 32, 66, 80 },
  { FONT_KEY_GOTHIC_18_BOLD, 20, 23, 50, 62 },
  { FONT_KEY_GOTHIC_14_BOLD, 16,  0, 40, 50 },
};
#define NUM_ROW_FONTS ((int)(sizeof(ROW_FONTS) / sizeof(ROW_FONTS[0])))

static const RowFont *row_font_for(int row_h) {
  for (int i = 0; i < NUM_ROW_FONTS; i++) {
    if (row_h >= ROW_FONTS[i].min_row_h) return &ROW_FONTS[i];
  }
  return &ROW_FONTS[NUM_ROW_FONTS - 1];  // unreachable: last entry has min 0
}

static int header_height(int screen_h) {
  return (screen_h > 168) ? 34 : 26;
}

// -----------------------------------------------------------------------------
// Config persistence
// -----------------------------------------------------------------------------

static void set_label(char *dst, const char *src) {
  strncpy(dst, src, MAX_LABEL_LEN);
  dst[MAX_LABEL_LEN] = '\0';
}

static void load_defaults(void) {
  s_local_offset_min = 0;
  s_h24 = clock_is_24h_style();
  s_dark = false;
  set_label(s_local_label, "LOCAL");
  memset(s_zones, 0, sizeof(s_zones));
  s_num_zones = 3;
  set_label(s_zones[0].label, "NYC"); s_zones[0].offset_min = -300;
  set_label(s_zones[1].label, "LDN"); s_zones[1].offset_min = 0;
  set_label(s_zones[2].label, "TYO"); s_zones[2].offset_min = 540;
}

static void load_config(void) {
  if (!persist_exists(PERSIST_NUM_ZONES) || !persist_exists(PERSIST_ZONES_BLOB)) {
    load_defaults();
    return;
  }

  s_num_zones = persist_read_int(PERSIST_NUM_ZONES);
  if (s_num_zones < 0)         s_num_zones = 0;
  if (s_num_zones > MAX_ZONES) s_num_zones = MAX_ZONES;
  persist_read_data(PERSIST_ZONES_BLOB, s_zones, sizeof(s_zones));

  if (persist_exists(PERSIST_LOCAL_OFFSET)) {
    s_local_offset_min = persist_read_int(PERSIST_LOCAL_OFFSET);
  }
  if (persist_exists(PERSIST_H24)) {
    s_h24 = persist_read_int(PERSIST_H24) != 0;
  } else {
    s_h24 = clock_is_24h_style();
  }
  if (persist_exists(PERSIST_DARK)) {
    s_dark = persist_read_int(PERSIST_DARK) != 0;
  }
  if (persist_exists(PERSIST_LOCAL_LABEL)) {
    // Truncation is fine here; a short buffer just means a shorter label.
    persist_read_string(PERSIST_LOCAL_LABEL, s_local_label, sizeof(s_local_label));
  }
  // A blob written by an older/corrupt version could leave a label unterminated.
  for (int i = 0; i < MAX_ZONES; i++) {
    s_zones[i].label[MAX_LABEL_LEN] = '\0';
  }
}

static void save_config(void) {
  persist_write_int(PERSIST_LOCAL_OFFSET, s_local_offset_min);
  persist_write_int(PERSIST_NUM_ZONES,    s_num_zones);
  persist_write_int(PERSIST_H24,          s_h24 ? 1 : 0);
  persist_write_int(PERSIST_DARK,         s_dark ? 1 : 0);
  persist_write_string(PERSIST_LOCAL_LABEL, s_local_label);
  persist_write_data(PERSIST_ZONES_BLOB,  s_zones, sizeof(s_zones));
}

// -----------------------------------------------------------------------------
// Time helpers
// -----------------------------------------------------------------------------

// Days since the epoch as seen in a zone `off_min` east of UTC. Used only for
// differencing two zones, so the absolute value doesn't matter — but it does
// have to floor towards negative infinity for pre-1970 sanity.
static int days_in_zone(time_t utc_now, int32_t off_min) {
  long long shifted = (long long)utc_now + (long long)off_min * 60LL;
  long long d = shifted / 86400LL;
  if (shifted < 0 && (shifted % 86400LL) != 0) d--;
  return (int)d;
}

static void format_zone_time(time_t utc_now, int32_t off_min, char *out, int n) {
  time_t shifted = utc_now + (time_t)off_min * 60;
  struct tm *t = gmtime(&shifted);
  if (s_h24) {
    snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    int h = t->tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(out, n, "%d:%02d%s", h, t->tm_min, t->tm_hour >= 12 ? "p" : "a");
  }
}

// -----------------------------------------------------------------------------
// Row model
// -----------------------------------------------------------------------------
//
// build_rows() flattens the config into the exact list the renderer draws, so
// the drawing code never has to special-case local vs UTC vs configured zone.

typedef struct {
  const char *label;
  int32_t     offset_min;
  GColor      bg;
  GColor      fg;
} Row;

static int build_rows(Row *rows, int cap) {
  int n = 0;

  if (n < cap) {
    rows[n++] = (Row){ .label = s_local_label, .offset_min = s_local_offset_min,
                       .bg = GColorRed, .fg = GColorWhite };
  }
  if (n < cap) {
    rows[n++] = (Row){ .label = "UTC", .offset_min = 0,
                       .bg = GColorBlue, .fg = GColorWhite };
  }
  // A configured zone that currently resolves to the local or UTC offset would
  // just repeat a band that's already on screen — Lisbon in winter is UTC, and
  // your own zone is a common pick — so drop it. This is deliberately checked
  // against the live offset rather than the IANA name: the same zone can
  // collide for half the year and separate again when DST shifts.
  for (int i = 0; i < s_num_zones && i < MAX_ZONES && n < cap; i++) {
    int32_t off = s_zones[i].offset_min;
    if (off == s_local_offset_min || off == 0) continue;
    rows[n++] = (Row){ .label = s_zones[i].label, .offset_min = off,
                       .bg = theme_bg(), .fg = theme_fg() };
  }

  // Sort east-to-west: everything ahead of UTC sits above the UTC band, and
  // everything behind it sits below. The local band takes whatever position its
  // own offset earns. Insertion sort — n is at most MAX_ROWS, and it's stable,
  // so zones sharing an offset with UTC keep the order they were configured in.
  for (int i = 1; i < n; i++) {
    Row key = rows[i];
    int j = i - 1;
    while (j >= 0 && rows[j].offset_min < key.offset_min) {
      rows[j + 1] = rows[j];
      j--;
    }
    rows[j + 1] = key;
  }
  return n;
}

// -----------------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------------

static void header_layer_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  bool large = (b.size.h > 30);

  graphics_context_set_fill_color(ctx, theme_bg());
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Date in the local zone. gmtime() on the offset-shifted timestamp gives the
  // local wall clock, with tm_wday/tm_mon filled in for strftime.
  time_t local_t = time(NULL) + (time_t)s_local_offset_min * 60;
  struct tm *t = gmtime(&local_t);
  char date_buf[20];
  strftime(date_buf, sizeof(date_buf), "%a %d %b", t);

  GFont font = fonts_get_system_font(large ? FONT_KEY_GOTHIC_24_BOLD
                                           : FONT_KEY_GOTHIC_18_BOLD);
  int line_h = large ? 26 : 20;
  graphics_context_set_text_color(ctx, theme_fg());
  graphics_draw_text(ctx, date_buf, font,
      GRect(4, (b.size.h - line_h) / 2 - 3, b.size.w - 8, line_h + 6),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Hairline under the header, separating it from the first band.
  graphics_context_set_stroke_color(ctx, theme_rule());
  graphics_draw_line(ctx, GPoint(0, b.size.h - 1), GPoint(b.size.w, b.size.h - 1));
}

static void rows_layer_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  Row rows[MAX_ROWS];
  int n = build_rows(rows, MAX_ROWS);
  if (n <= 0) return;

  // Bands are laid out by interpolating the band edges across the full body
  // height. That spreads the leftover pixels evenly and, more importantly,
  // leaves no uncoloured seams between adjacent bands.
  int body_h = bounds.size.h;
  int row_h  = body_h / n;

  const RowFont *rf = row_font_for(row_h);
  GFont  font   = fonts_get_system_font(rf->key);
  int    time_w = s_h24 ? rf->time_w_24 : rf->time_w_12;

  time_t utc_now = time(NULL);
  int    local_day = days_in_zone(utc_now, s_local_offset_min);

  for (int i = 0; i < n; i++) {
    int y0 = (body_h * i) / n;
    int y1 = (body_h * (i + 1)) / n;
    int h  = y1 - y0;

    graphics_context_set_fill_color(ctx, rows[i].bg);
    graphics_fill_rect(ctx, GRect(0, y0, bounds.size.w, h), 0, GCornerNone);

    // Adjacent bands of the same colour need a rule to read as separate rows;
    // the red/blue bands already separate themselves.
    if (i > 0 && gcolor_equal(rows[i].bg, rows[i - 1].bg)) {
      graphics_context_set_stroke_color(ctx, theme_rule());
      graphics_draw_line(ctx, GPoint(4, y0), GPoint(bounds.size.w - 4, y0));
    }

    int diff = days_in_zone(utc_now, rows[i].offset_min) - local_day;
    char label_buf[MAX_LABEL_LEN + 8];
    if (diff == 0) {
      snprintf(label_buf, sizeof(label_buf), "%s", rows[i].label);
    } else {
      snprintf(label_buf, sizeof(label_buf), "%s %+d", rows[i].label, diff);
    }

    char time_buf[10];
    format_zone_time(utc_now, rows[i].offset_min, time_buf, sizeof(time_buf));

    // -3 / +6 compensates for the top padding Pebble's gothic fonts carry, so
    // the glyphs sit optically centred in the band.
    int text_y = y0 + (h - rf->line_h) / 2 - 3;
    int text_h = rf->line_h + 6;

    graphics_context_set_text_color(ctx, rows[i].fg);
    graphics_draw_text(ctx, label_buf, font,
        GRect(5, text_y, bounds.size.w - time_w - 8, text_h),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, time_buf, font,
        GRect(bounds.size.w - time_w - 5, text_y, time_w, text_h),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
}

static void redraw_all(void) {
  layer_mark_dirty(s_header_layer);
  layer_mark_dirty(s_rows_layer);
}

// -----------------------------------------------------------------------------
// Handlers
// -----------------------------------------------------------------------------

static void request_config_refresh(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int32(iter, MESSAGE_KEY_REQUEST_CONFIG, 1);
  app_message_outbox_send();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time;
  (void)units_changed;
  redraw_all();
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  (void)context;
  bool changed = false;

  Tuple *t = dict_find(iter, MESSAGE_KEY_LOCAL_OFFSET);
  if (t) { s_local_offset_min = t->value->int32; changed = true; }

  t = dict_find(iter, MESSAGE_KEY_LOCAL_LABEL);
  if (t && t->type == TUPLE_CSTRING) {
    set_label(s_local_label, t->value->cstring);
    changed = true;
  }

  t = dict_find(iter, MESSAGE_KEY_H24);
  if (t) { s_h24 = (t->value->int32 != 0); changed = true; }

  t = dict_find(iter, MESSAGE_KEY_DARK);
  if (t) { s_dark = (t->value->int32 != 0); changed = true; }

  t = dict_find(iter, MESSAGE_KEY_NUM_ZONES);
  if (t) {
    int n = t->value->int32;
    if (n < 0)         n = 0;
    if (n > MAX_ZONES) n = MAX_ZONES;
    s_num_zones = n;
    changed = true;
  }

  for (int i = 0; i < MAX_ZONES; i++) {
    t = dict_find(iter, kZLabelKey[i]);
    if (t && t->type == TUPLE_CSTRING) {
      set_label(s_zones[i].label, t->value->cstring);
      changed = true;
    }
    t = dict_find(iter, kZOffsetKey[i]);
    if (t) {
      s_zones[i].offset_min = t->value->int32;
      changed = true;
    }
  }

  if (!changed) return;

  APP_LOG(APP_LOG_LEVEL_INFO, "config updated: zones=%d local_off=%ld h24=%d dark=%d",
          s_num_zones, (long)s_local_offset_min, (int)s_h24, (int)s_dark);
  save_config();
  window_set_background_color(s_window, theme_bg());
  redraw_all();
}

// -----------------------------------------------------------------------------
// Window / app lifecycle
// -----------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root  = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int   hdr_h  = header_height(bounds.size.h);

  window_set_background_color(window, theme_bg());

  s_header_layer = layer_create(GRect(0, 0, bounds.size.w, hdr_h));
  layer_set_update_proc(s_header_layer, header_layer_update);
  layer_add_child(root, s_header_layer);

  s_rows_layer = layer_create(GRect(0, hdr_h, bounds.size.w, bounds.size.h - hdr_h));
  layer_set_update_proc(s_rows_layer, rows_layer_update);
  layer_add_child(root, s_rows_layer);
}

static void window_unload(Window *window) {
  (void)window;
  layer_destroy(s_header_layer);
  layer_destroy(s_rows_layer);
}

static void init(void) {
  init_message_keys();
  load_config();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(512, 64);

  request_config_refresh();
}

static void deinit(void) {
  save_config();
  app_message_deregister_callbacks();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
