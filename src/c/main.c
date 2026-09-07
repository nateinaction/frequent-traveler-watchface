// Frequent Traveller — a Pebble watchface for people who live across
// timezones.
//
// The whole screen is a stack of full-width color bands, one per timezone, each
// in a color you pick in Settings. There is no header: your local zone is a
// band like any other, just pinned to the top, and it carries the date where
// the other bands carry their label.
//
// The bands below it are ordered east to west by UTC offset: every zone ahead
// of UTC sits above every zone behind it. UTC is only the reference the sort is
// against — it gets a band of its own only if you add it as a zone. A
// configured zone that currently resolves to your local offset is hidden rather
// than drawn as a duplicate of the local band.
//
// The background is white, and so is a band whose color hasn't been changed.
//
// Any row whose calendar date differs from your local date gets a "+1" / "-1"
// suffix on its label.
//
// The watch itself doesn't know about IANA timezones or DST. The phone config
// page resolves each zone's current UTC offset (in minutes) and ships it down
// over AppMessage; see src/pkjs/index.js for why that resolution can't happen
// in pkjs itself.

#include <pebble.h>

// The local band plus the configured zones, so MAX_ROWS bounds every row loop
// in this file.
#define MAX_ZONES 6
#define MAX_ROWS (MAX_ZONES + 1)
#define MAX_LABEL_LEN 12
// Room for a label plus the " +1" / " -1" day marker, or for the local band's
// date ("Sun 06 Sep").
#define LABEL_BUF_LEN (MAX_LABEL_LEN + 8)

// A band nobody has recolored is white, like the background behind it; the
// seam rules are what keep such rows apart.
#define DEFAULT_COLOR_RGB 0xFFFFFF

#define PERSIST_LOCAL_OFFSET 1
#define PERSIST_NUM_ZONES 2
#define PERSIST_H24 4
#define PERSIST_LOCAL_COLOR 8
// Bumped from 3 when Zone gained a color: a blob written by the previous
// layout would deserialize into garbage offsets, so the old key is abandoned
// and a missing v2 blob just falls back to defaults.
#define PERSIST_ZONES_BLOB_V2 7

typedef struct {
  char label[MAX_LABEL_LEN + 1];
  int32_t offset_min;  // minutes east of UTC
  uint32_t color_rgb;  // 0xRRGGBB as picked on the phone
} Zone;

// AppMessage keys for per-slot zone values. A JS-side array fans out to
// individual keys because per-element tuples don't arrive reliably.
// MESSAGE_KEY_* are linker-time constants rather than compile-time ones, so
// these lookup tables are filled in at runtime by init_message_keys().
static uint32_t kZLabelKey[MAX_ZONES];
static uint32_t kZOffsetKey[MAX_ZONES];
static uint32_t kZColorKey[MAX_ZONES];

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
  kZColorKey[0] = MESSAGE_KEY_Z_COLOR_0;
  kZColorKey[1] = MESSAGE_KEY_Z_COLOR_1;
  kZColorKey[2] = MESSAGE_KEY_Z_COLOR_2;
  kZColorKey[3] = MESSAGE_KEY_Z_COLOR_3;
  kZColorKey[4] = MESSAGE_KEY_Z_COLOR_4;
  kZColorKey[5] = MESSAGE_KEY_Z_COLOR_5;
}

// ---- state ----
static Window *s_window;
static Layer *s_rows_layer;

static Zone s_zones[MAX_ZONES];
static int s_num_zones = 0;
static int32_t s_local_offset_min = 0;
static uint32_t s_local_color_rgb = DEFAULT_COLOR_RGB;
static bool s_h24 = true;

// -----------------------------------------------------------------------------
// Theme
// -----------------------------------------------------------------------------
//
// With no header left, the theme only shows through in the seams between bands
// and in whatever is behind them. Band colors come from the config, per zone.

static GColor theme_bg(void) {
  return GColorWhite;
}
#ifndef PBL_COLOR
// Only black-and-white watches need a foreground from the theme: on a color
// watch every glyph sits on a band and takes its contrast from the band color.
static GColor theme_fg(void) {
  return GColorBlack;
}
#endif
static GColor theme_rule(void) {
#ifdef PBL_COLOR
  return GColorLightGray;
#else
  // No gray to make a subtle rule from, and on a black-and-white watch every
  // band is the same color — the seams are the only thing keeping rows apart.
  return theme_fg();
#endif
}

// -----------------------------------------------------------------------------
// Band colors
// -----------------------------------------------------------------------------
//
// The phone sends 24-bit RGB. Color watches quantize it to their 64-color
// palette; black-and-white watches can't honour it at all, so they fall back to
// the theme and rely on the seam rules to keep rows apart.

static GColor band_bg(uint32_t rgb) {
#ifdef PBL_COLOR
  return GColorFromRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
#else
  (void)rgb;
  return theme_bg();
#endif
}

// Rec. 601 luma, thresholded: dark bands take white text, light bands black.
static GColor band_fg(uint32_t rgb) {
#ifdef PBL_COLOR
  uint32_t luma =
      (299 * ((rgb >> 16) & 0xFF) + 587 * ((rgb >> 8) & 0xFF) + 114 * (rgb & 0xFF)) /
      1000;
  return (luma >= 140) ? GColorBlack : GColorWhite;
#else
  (void)rgb;
  return theme_fg();
#endif
}

// -----------------------------------------------------------------------------
// Row fonts
// -----------------------------------------------------------------------------
//
// Rows share the body evenly, so the font has to shrink as zones are added.
// line_h is the drawn height of the font (used to centre text in its band) and
// time_w is the column reserved for the right-hand clock, which is wider in
// 12-hour mode because of the trailing a/p.
//
// Height is only half the constraint: on a 144px screen a two-row layout has
// room for 28pt bands but not for "TYO +1" beside a 28pt clock, so the chosen
// font also has to leave the widest label its full width (see fit_row_font).
//
// The date on the local band is exempt from that: it shortens itself to fit the
// font instead of dragging every row down a size (see local_date).

typedef struct {
  const char *key;
  int line_h;
  int min_row_h;  // smallest band this font is allowed in
  int time_w_24;
  int time_w_12;
} RowFont;

static const RowFont ROW_FONTS[] = {
    {FONT_KEY_GOTHIC_28_BOLD, 30, 40, 76, 92},
    {FONT_KEY_GOTHIC_24_BOLD, 26, 32, 66, 80},
    {FONT_KEY_GOTHIC_18_BOLD, 20, 23, 50, 62},
    {FONT_KEY_GOTHIC_14_BOLD, 16, 0, 40, 50},
};
#define NUM_ROW_FONTS ((int)(sizeof(ROW_FONTS) / sizeof(ROW_FONTS[0])))

static int row_font_for(int row_h) {
  for (int i = 0; i < NUM_ROW_FONTS; i++) {
    if (row_h >= ROW_FONTS[i].min_row_h)
      return i;
  }
  return NUM_ROW_FONTS - 1;  // unreachable: last entry has min 0
}

static int row_time_w(const RowFont *rf) {
  return s_h24 ? rf->time_w_24 : rf->time_w_12;
}

// -----------------------------------------------------------------------------
// Config persistence
// -----------------------------------------------------------------------------

static void set_label(char *dst, const char *src) {
  strncpy(dst, src, MAX_LABEL_LEN);
  dst[MAX_LABEL_LEN] = '\0';
}

// The out-of-the-box demo: black bands with UTC picked out in blue, under a
// white local band. Zones the user adds later default to white instead
// (DEFAULT_COLOR_RGB); these carry colors so the face means something before
// Settings has ever been opened. The offsets are a standing-in guess — the
// phone replaces them with DST-correct ones on the first config message.
static void load_defaults(void) {
  s_local_offset_min = 0;
  s_local_color_rgb = DEFAULT_COLOR_RGB;
  s_h24 = clock_is_24h_style();
  memset(s_zones, 0, sizeof(s_zones));
  s_num_zones = 5;
  set_label(s_zones[0].label, "PAR");
  s_zones[0].offset_min = 120;
  s_zones[0].color_rgb = 0x000000;
  set_label(s_zones[1].label, "TYO");
  s_zones[1].offset_min = 540;
  s_zones[1].color_rgb = 0x000000;
  set_label(s_zones[2].label, "UTC");
  s_zones[2].offset_min = 0;
  s_zones[2].color_rgb = 0x0055AA;
  set_label(s_zones[3].label, "NYC");
  s_zones[3].offset_min = -240;
  s_zones[3].color_rgb = 0x000000;
  set_label(s_zones[4].label, "LAX");
  s_zones[4].offset_min = -420;
  s_zones[4].color_rgb = 0x000000;
}

static void load_config(void) {
  if (!persist_exists(PERSIST_NUM_ZONES) || !persist_exists(PERSIST_ZONES_BLOB_V2)) {
    load_defaults();
    return;
  }

  s_num_zones = persist_read_int(PERSIST_NUM_ZONES);
  if (s_num_zones < 0)
    s_num_zones = 0;
  if (s_num_zones > MAX_ZONES)
    s_num_zones = MAX_ZONES;
  persist_read_data(PERSIST_ZONES_BLOB_V2, s_zones, sizeof(s_zones));

  if (persist_exists(PERSIST_LOCAL_OFFSET)) {
    s_local_offset_min = persist_read_int(PERSIST_LOCAL_OFFSET);
  }
  if (persist_exists(PERSIST_LOCAL_COLOR)) {
    s_local_color_rgb = (uint32_t)persist_read_int(PERSIST_LOCAL_COLOR) & 0xFFFFFF;
  }
  if (persist_exists(PERSIST_H24)) {
    s_h24 = persist_read_int(PERSIST_H24) != 0;
  } else {
    s_h24 = clock_is_24h_style();
  }
  // A blob written by an older/corrupt version could leave a label unterminated
  // or a color with junk in its high byte.
  for (int i = 0; i < MAX_ZONES; i++) {
    s_zones[i].label[MAX_LABEL_LEN] = '\0';
    s_zones[i].color_rgb &= 0xFFFFFF;
  }
}

static void save_config(void) {
  persist_write_int(PERSIST_LOCAL_OFFSET, s_local_offset_min);
  persist_write_int(PERSIST_LOCAL_COLOR, (int32_t)s_local_color_rgb);
  persist_write_int(PERSIST_NUM_ZONES, s_num_zones);
  persist_write_int(PERSIST_H24, s_h24 ? 1 : 0);
  persist_write_data(PERSIST_ZONES_BLOB_V2, s_zones, sizeof(s_zones));
}

// -----------------------------------------------------------------------------
// Time helpers
// -----------------------------------------------------------------------------

// Days since the epoch as seen in a zone `off_min` east of UTC. Used only for
// differencing two zones, so the absolute value doesn't matter — but it does
// have to floor towards negative infinity for pre-1970 sanity.
static int days_in_zone(time_t utc_now, int32_t off_min) {
  int64_t shifted = (int64_t)utc_now + (int64_t)off_min * 60LL;
  int64_t d = shifted / 86400LL;
  if (shifted < 0 && (shifted % 86400LL) != 0)
    d--;
  return (int)d;
}

static void format_zone_time(time_t utc_now, int32_t off_min, char *out, int n) {
  time_t shifted = utc_now + (time_t)off_min * 60;
  struct tm *t = gmtime(&shifted);
  if (s_h24) {
    snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    int h = t->tm_hour % 12;
    if (h == 0)
      h = 12;
    snprintf(out, n, "%d:%02d%s", h, t->tm_min, t->tm_hour >= 12 ? "p" : "a");
  }
}

// -----------------------------------------------------------------------------
// Row model
// -----------------------------------------------------------------------------
//
// build_rows() flattens the config into the exact list the renderer draws —
// hidden zones removed, colors resolved, already in display order — so the
// drawing code never has to consult the config at all.

typedef struct {
  const char *label;  // NULL on the local row, whose label is the date
  int32_t offset_min;
  GColor bg;
  GColor fg;
} Row;

static int build_rows(Row *rows, int cap) {
  int n = 0;

  // The local band is pinned to row 0 rather than sorted in by its offset: it's
  // the one you read first, and it carries the date for the whole face.
  if (n < cap) {
    rows[n++] = (Row){.label = NULL,
                      .offset_min = s_local_offset_min,
                      .bg = band_bg(s_local_color_rgb),
                      .fg = band_fg(s_local_color_rgb)};
  }

  // A configured zone that currently resolves to your local offset would just
  // repeat the local band, so drop it. This is deliberately checked against the
  // live offset rather than the IANA name: the same zone can collide for half
  // the year and separate again when DST shifts.
  for (int i = 0; i < s_num_zones && i < MAX_ZONES && n < cap; i++) {
    int32_t off = s_zones[i].offset_min;
    if (off == s_local_offset_min)
      continue;
    rows[n++] = (Row){.label = s_zones[i].label,
                      .offset_min = off,
                      .bg = band_bg(s_zones[i].color_rgb),
                      .fg = band_fg(s_zones[i].color_rgb)};
  }

  // Sort east-to-west against UTC as the reference: every zone ahead of UTC
  // sits above every zone behind it. UTC itself only appears if the user added
  // it. Insertion sort over rows[1..n) — row 0 is the pinned local band and
  // stays put. n is at most MAX_ROWS, and the sort is stable, so zones sharing
  // an offset keep the order they were configured in.
  for (int i = 2; i < n; i++) {
    Row key = rows[i];
    int j = i - 1;
    while (j >= 1 && rows[j].offset_min < key.offset_min) {
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

// Widest of the already-formatted labels, in pixels, at `font`.
static int widest_label(char labels[][LABEL_BUF_LEN], int n, GFont font) {
  int widest = 0;
  for (int i = 0; i < n; i++) {
    GSize s = graphics_text_layout_get_content_size(
        labels[i], font, GRect(0, 0, 1000, 40), GTextOverflowModeWordWrap,
        GTextAlignmentLeft);
    if (s.w > widest)
      widest = s.w;
  }
  return widest;
}

// Step down from the tallest font the bands can hold until the widest label
// fits beside the clock. Bounded by the font table, and the last entry is small
// enough that the loop always terminates on a real fit or on that entry.
static int fit_row_font(int start, char labels[][LABEL_BUF_LEN], int n, int screen_w) {
  for (int i = start; i < NUM_ROW_FONTS - 1; i++) {
    GFont font = fonts_get_system_font(ROW_FONTS[i].key);
    int avail = screen_w - row_time_w(&ROW_FONTS[i]) - 10;
    if (widest_label(labels, n, font) <= avail)
      return i;
  }
  return NUM_ROW_FONTS - 1;
}

// The local band's label is the date, written into `out` in the longest form
// that fits `avail` pixels at `font`. A clipped date is worse than one without
// the weekday, and shortening it here keeps the date from forcing every row
// down a font size.
static void local_date(time_t utc_now, GFont font, int avail, char *out, int n) {
  static const char *const FORMATS[] = {"%a %d %b", "%d %b", "%d"};
  const int num_formats = (int)(sizeof(FORMATS) / sizeof(FORMATS[0]));

  // gmtime() on the offset-shifted timestamp gives the local wall clock, with
  // tm_wday/tm_mon filled in for strftime.
  time_t local_t = utc_now + (time_t)s_local_offset_min * 60;
  struct tm *t = gmtime(&local_t);

  for (int i = 0; i < num_formats; i++) {
    strftime(out, n, FORMATS[i], t);
    GSize size = graphics_text_layout_get_content_size(
        out, font, GRect(0, 0, 1000, 40), GTextOverflowModeWordWrap, GTextAlignmentLeft);
    if (size.w <= avail)
      return;
  }
  // Out of formats: the shortest one stays, and the draw ellipsizes it.
}

static void rows_layer_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  Row rows[MAX_ROWS];
  int n = build_rows(rows, MAX_ROWS);
  if (n <= 0)
    return;  // Unreachable: the local band is always row 0.

  time_t utc_now = time(NULL);
  int local_day = days_in_zone(utc_now, s_local_offset_min);

  // Zone labels are formatted before the font is chosen, because the day marker
  // is what pushes a label past the width the big fonts can afford. Row 0 is
  // the local band; its date is formatted once the font is known.
  char labels[MAX_ROWS][LABEL_BUF_LEN];
  for (int i = 1; i < n; i++) {
    int diff = days_in_zone(utc_now, rows[i].offset_min) - local_day;
    if (diff == 0) {
      snprintf(labels[i], LABEL_BUF_LEN, "%s", rows[i].label);
    } else {
      snprintf(labels[i], LABEL_BUF_LEN, "%s %+d", rows[i].label, diff);
    }
  }

  // Bands are laid out by interpolating the band edges across the full body
  // height. That spreads the leftover pixels evenly and, more importantly,
  // leaves no uncolored seams between adjacent bands.
  int body_h = bounds.size.h;
  int row_h = body_h / n;

  const RowFont *rf =
      &ROW_FONTS[fit_row_font(row_font_for(row_h), labels + 1, n - 1, bounds.size.w)];
  GFont font = fonts_get_system_font(rf->key);
  int time_w = row_time_w(rf);

  local_date(utc_now, font, bounds.size.w - time_w - 10, labels[0], LABEL_BUF_LEN);

  for (int i = 0; i < n; i++) {
    int y0 = (body_h * i) / n;
    int y1 = (body_h * (i + 1)) / n;
    int h = y1 - y0;

    graphics_context_set_fill_color(ctx, rows[i].bg);
    graphics_fill_rect(ctx, GRect(0, y0, bounds.size.w, h), 0, GCornerNone);

    // Adjacent bands of the same color need a rule to read as separate rows;
    // bands of different colors already separate themselves.
    if (i > 0 && gcolor_equal(rows[i].bg, rows[i - 1].bg)) {
      graphics_context_set_stroke_color(ctx, theme_rule());
      graphics_draw_line(ctx, GPoint(4, y0), GPoint(bounds.size.w - 4, y0));
    }

    char time_buf[10];
    format_zone_time(utc_now, rows[i].offset_min, time_buf, sizeof(time_buf));

    // -3 / +6 compensates for the top padding Pebble's gothic fonts carry, so
    // the glyphs sit optically centred in the band.
    int text_y = y0 + (h - rf->line_h) / 2 - 3;
    int text_h = rf->line_h + 6;

    graphics_context_set_text_color(ctx, rows[i].fg);
    graphics_draw_text(ctx, labels[i], font,
                       GRect(5, text_y, bounds.size.w - time_w - 8, text_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, time_buf, font,
                       GRect(bounds.size.w - time_w - 5, text_y, time_w, text_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
}

static void redraw_all(void) {
  layer_mark_dirty(s_rows_layer);
}

// -----------------------------------------------------------------------------
// Handlers
// -----------------------------------------------------------------------------

static void request_config_refresh(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK)
    return;
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
  if (t) {
    s_local_offset_min = t->value->int32;
    changed = true;
  }

  t = dict_find(iter, MESSAGE_KEY_LOCAL_COLOR);
  if (t) {
    s_local_color_rgb = (uint32_t)t->value->int32 & 0xFFFFFF;
    changed = true;
  }

  t = dict_find(iter, MESSAGE_KEY_H24);
  if (t) {
    s_h24 = (t->value->int32 != 0);
    changed = true;
  }

  t = dict_find(iter, MESSAGE_KEY_NUM_ZONES);
  if (t) {
    int n = t->value->int32;
    if (n < 0)
      n = 0;
    if (n > MAX_ZONES)
      n = MAX_ZONES;
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
    t = dict_find(iter, kZColorKey[i]);
    if (t) {
      s_zones[i].color_rgb = (uint32_t)t->value->int32 & 0xFFFFFF;
      changed = true;
    }
  }

  if (!changed)
    return;

  APP_LOG(APP_LOG_LEVEL_INFO, "config updated: zones=%d local_off=%d h24=%d", s_num_zones,
          (int)s_local_offset_min, (int)s_h24);
  save_config();
  redraw_all();
}

// -----------------------------------------------------------------------------
// Window / app lifecycle
// -----------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  window_set_background_color(window, theme_bg());

  s_rows_layer = layer_create(bounds);
  layer_set_update_proc(s_rows_layer, rows_layer_update);
  layer_add_child(root, s_rows_layer);
}

static void window_unload(Window *window) {
  (void)window;
  layer_destroy(s_rows_layer);
}

static void init(void) {
  init_message_keys();
  load_config();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
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
