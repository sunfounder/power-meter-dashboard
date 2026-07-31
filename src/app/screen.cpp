#include "screen.h"
#include "config.h"
#include "measure.h"
#include "record.h"
#include "../hal/buzzer.h"
#include "../hal/pin_config.h"
#include "../ui/ui.h"

#include <WiFi.h>
#include <time.h>

static lv_obj_t *_page = nullptr;
static lv_obj_t *_cards[4];
static lv_obj_t *_val_labels[4][4];
static lv_obj_t *_unit_labels[4][4];
static lv_obj_t *_conn_bars[4];
static lv_obj_t *_status_dots[4];
static lv_obj_t *_ambient_label = nullptr;
static lv_obj_t *_time_label = nullptr;
static lv_obj_t *_wifi_ssid_label = nullptr;
static lv_obj_t *_wifi_ip_label = nullptr;

// Scope mode
static lv_obj_t *_scope_page = nullptr;
static lv_obj_t *_scope_chart = nullptr;
static lv_timer_t *_scope_timer = nullptr;
static int _scope_ch = 2; // default CH3
static lv_chart_series_t *_scope_ser_v = nullptr;
static lv_chart_series_t *_scope_ser_a = nullptr;
static lv_obj_t *_scope_val_label = nullptr;
static lv_timer_t *_refresh_timer = nullptr;
static lv_timer_t *_elapsed_timer = nullptr;
static bool _dot_visible = true;

static void no_scroll(lv_obj_t *obj) {
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t *create_card(lv_obj_t *parent, int x, int w, int h, int ch_num) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_set_pos(card, x, 22);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x151515), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 6, 0);
  lv_obj_set_style_clip_corner(card, true, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  no_scroll(card);

  // Title bar ¡ª flat top corners (card clips them), sharp bottom
  lv_obj_t *bar = lv_obj_create(card);
  lv_obj_set_size(bar, w, 18);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0xE27005), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  no_scroll(bar);
  _conn_bars[ch_num - 1] = bar;

  // Channel label ¡ª left-aligned
  char name[12];
  snprintf(name, sizeof(name), "CH%d", ch_num);
  lv_obj_t *tl = lv_label_create(bar);
  lv_label_set_text(tl, name);
  lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
  lv_obj_align(tl, LV_ALIGN_LEFT_MID, 4, 0);

  // Status dot ¡ª right side
  lv_obj_t *dot = lv_obj_create(bar);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0x444444), 0); // IDLE gray
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_radius(dot, 5, 0);
  lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -4, 0);
  no_scroll(dot);
  _status_dots[ch_num - 1] = dot;

  return card;
}

// Add a metric row: value + colored unit badge
static void add_metric(lv_obj_t *card, int ch, int idx, int w, int y) {
  lv_obj_t *row = lv_obj_create(card);
  lv_obj_set_size(row, w, 20);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Value
  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, "---");
  lv_obj_set_style_text_color(val, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_recolor(val, true);
  lv_obj_set_flex_grow(val, 1);
  _val_labels[ch][idx] = val;

  // Unit badge
  static const lv_color_t badge_colors[] = {
    lv_color_hex(0xE27005), lv_color_hex(0x3CB84C), lv_color_hex(0x4895EF), lv_color_hex(0xE0C040)
  };
  lv_obj_t *unit = lv_label_create(row);
  static const char *units[] = {"V", "A", "W", "C"};
  lv_label_set_text(unit, units[idx]);
  lv_obj_set_style_text_color(unit, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(unit, badge_colors[idx], 0);
  lv_obj_set_style_bg_opa(unit, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(unit, 4, 0);
  lv_obj_set_style_pad_ver(unit, 1, 0);
  lv_obj_set_style_radius(unit, 0, 0);
  lv_obj_set_width(unit, 26);  // fixed width for alignment
  _unit_labels[ch][idx] = unit;
}

static void refresh_card(int ch) {
  if (!_cards[ch]) return;
  auto &s = MeasurementEngine::getInstance().getLatest().channels[ch];
  char buf[24];

  // Always update temperature unit label
  lv_label_set_text(_unit_labels[ch][3], DeviceSettings::getInstance().tempUnit()=='F' ? "F" : "C");

  if (!s.connected) {
    lv_label_set_text(_val_labels[ch][0], "---");
    lv_label_set_text(_val_labels[ch][1], "---");
    lv_label_set_text(_val_labels[ch][2], "---");
    lv_label_set_text(_val_labels[ch][3], "---");
    for (int j = 0; j < 4; j++) lv_obj_set_style_bg_color(_unit_labels[ch][j], lv_color_hex(0x333333), 0);
  } else {
    static const lv_color_t badge_colors[] = {
      lv_color_hex(0xE27005), lv_color_hex(0x3CB84C), lv_color_hex(0x4895EF), lv_color_hex(0xE0C040)
    };
    for (int j = 0; j < 4; j++) lv_obj_set_style_bg_color(_unit_labels[ch][j], badge_colors[j], 0);
    snprintf(buf, sizeof(buf), "%.2f", s.bus_voltage_V);
    lv_label_set_text(_val_labels[ch][0], buf);
    snprintf(buf, sizeof(buf), "%.3f", s.current_mA / 1000.0f);
    lv_label_set_text(_val_labels[ch][1], buf);
    snprintf(buf, sizeof(buf), "%.2f", s.power_mW / 1000.0f);
    lv_label_set_text(_val_labels[ch][2], buf);
    float tc = DeviceSettings::getInstance().tempUnit()=='F' ? s.channel_temp_C * 9/5 + 32 : s.channel_temp_C;
    snprintf(buf, sizeof(buf), "%.1f", tc);
    lv_label_set_text(_val_labels[ch][3], s.channel_temp_C < -100 ? "---" : buf);
    // Update unit label
    lv_label_set_text(_unit_labels[ch][3], DeviceSettings::getInstance().tempUnit()=='F' ? "F" : "C");
  }

  // Connection bar color
  if (_conn_bars[ch]) {
    lv_obj_set_style_bg_color(_conn_bars[ch],
      s.connected ? lv_color_hex(0xE27005) : lv_color_hex(0x333333), 0);
  }
  // Status dot
  if (_status_dots[ch]) {
    bool rec = DataRecorder::getInstance().isChannelRecording(ch);
    lv_obj_set_style_bg_color(_status_dots[ch],
      rec ? lv_color_hex(0xFF3333) : (s.connected ? lv_color_hex(0x444444) : lv_color_hex(0x222222)), 0);
  }
}

static void refresh_all_cards() {
  for (int i = 0; i < 4; i++) refresh_card(i);
}

static void refresh_status_bar() {
  auto &snap = MeasurementEngine::getInstance().getLatest();
  char buf[32];

  snprintf(buf, sizeof(buf), "%.1f'%c",
    DeviceSettings::getInstance().tempUnit()=='F' ? snap.env.ambient_temp_C * 9/5 + 32 : snap.env.ambient_temp_C,
    DeviceSettings::getInstance().tempUnit());
  lv_label_set_text(_ambient_label, buf);

  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text(_wifi_ssid_label, WiFi.SSID().c_str());
    lv_label_set_text(_wifi_ip_label, WiFi.localIP().toString().c_str());
  } else {
    lv_label_set_text(_wifi_ssid_label, WiFi.softAPSSID().c_str());
    lv_label_set_text(_wifi_ip_label, WiFi.softAPIP().toString().c_str());
  }
}

static void refresh_elapsed_time() {
  time_t now;
  time(&now);
  int8_t tz = DeviceSettings::getInstance().tzOffset();
  now += tz * 3600;  // apply timezone manually (more reliable than configTime)
  struct tm *tm = localtime(&now);
  char buf[8];
  if (now > 1600000000) {
    snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    snprintf(buf, sizeof(buf), "--:--");
  }
  lv_label_set_text(_time_label, buf);
}

static void refresh_timer_cb(lv_timer_t *) { refresh_all_cards(); refresh_status_bar(); }
static void elapsed_timer_cb(lv_timer_t *) { refresh_elapsed_time(); }

/* ---- App lifecycle ---- */

void power_meter_init() {
  _page = lv_obj_create(lv_scr_act());
  lv_obj_set_size(_page, 320, 170);
  lv_obj_set_style_bg_color(_page, lv_color_hex(0x050505), 0);
  lv_obj_set_style_border_width(_page, 0, 0);
  lv_obj_set_style_pad_all(_page, 0, 0);
  no_scroll(_page);

  // ---- Top bar (22px) ----
  lv_obj_t *bar = lv_obj_create(_page);
  lv_obj_set_size(bar, 320, 22);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x0C0C0C), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 2, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  no_scroll(bar);

  _time_label = lv_label_create(bar);
  lv_label_set_text(_time_label, "--:--");
  lv_obj_set_style_text_color(_time_label, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(_time_label, &lv_font_montserrat_14, 0);
  lv_obj_align(_time_label, LV_ALIGN_LEFT_MID, 4, 0);

  _ambient_label = lv_label_create(bar);
  lv_label_set_text(_ambient_label, "--.-'C");
  lv_obj_set_style_text_color(_ambient_label, lv_color_hex(0xBBBBBB), 0);
  lv_obj_set_style_text_font(_ambient_label, &lv_font_montserrat_14, 0);
  lv_obj_align(_ambient_label, LV_ALIGN_RIGHT_MID, -4, 0);

  // ---- Bottom bar (22px) ----
  lv_obj_t *wifi_bar = lv_obj_create(_page);
  lv_obj_set_size(wifi_bar, 320, 22);
  lv_obj_set_pos(wifi_bar, 0, 148);
  lv_obj_set_style_bg_color(wifi_bar, lv_color_hex(0x0C0C0C), 0);
  lv_obj_set_style_border_width(wifi_bar, 0, 0);
  lv_obj_set_style_pad_all(wifi_bar, 2, 0);
  lv_obj_set_style_radius(wifi_bar, 0, 0);
  no_scroll(wifi_bar);

  _wifi_ssid_label = lv_label_create(wifi_bar);
  lv_label_set_text(_wifi_ssid_label, "---");
  lv_obj_set_style_text_color(_wifi_ssid_label, lv_color_hex(0x777777), 0);
  lv_obj_set_style_text_font(_wifi_ssid_label, &lv_font_montserrat_14, 0);
  lv_obj_align(_wifi_ssid_label, LV_ALIGN_LEFT_MID, 4, 0);

  _wifi_ip_label = lv_label_create(wifi_bar);
  lv_label_set_text(_wifi_ip_label, "---");
  lv_obj_set_style_text_color(_wifi_ip_label, lv_color_hex(0x777777), 0);
  lv_obj_set_style_text_font(_wifi_ip_label, &lv_font_montserrat_14, 0);
  lv_obj_align(_wifi_ip_label, LV_ALIGN_RIGHT_MID, -4, 0);

  // ---- Cards: fill from y=22 to y=148 ----
  const int card_y = 22;
  const int card_h = 148 - card_y;  // = 126
  const int pad = 2;
  const int cw = (320 - pad * 5) / 4;  // 77.5 â†?77, evenly spaced
  // Recalc: total = 4*cw + 5*pad = 320, cw = (320-10)/4 = 77.5, use 78/77/78/77
  const int cws[4] = {78, 77, 78, 77};
  const int rows_y[] = {28, 50, 72, 94};  // below orange bar

  int x = pad;
  for (int i = 0; i < 4; i++) {
    _cards[i] = create_card(_page, x, cws[i], card_h, i + 1);
    for (int j = 0; j < 4; j++) {
      add_metric(_cards[i], i, j, cws[i], rows_y[j]);
    }
    x += cws[i] + pad;
  }

  _refresh_timer = lv_timer_create(refresh_timer_cb, 500, nullptr);
  _elapsed_timer = lv_timer_create(elapsed_timer_cb, 1000, nullptr);

  refresh_all_cards();
  refresh_status_bar();
}

void power_meter_hide() {
  if (_refresh_timer) { lv_timer_del(_refresh_timer); _refresh_timer = nullptr; }
  if (_elapsed_timer) { lv_timer_del(_elapsed_timer); _elapsed_timer = nullptr; }
  if (_page) { lv_obj_del(_page); _page = nullptr; }
  for (int i = 0; i < 4; i++) {
    _cards[i] = nullptr;
    for (int j = 0; j < 4; j++) _val_labels[i][j] = nullptr;
    for (int j = 0; j < 4; j++) _unit_labels[i][j] = nullptr;
  }
  _ambient_label = nullptr;
  _time_label = nullptr;
  _wifi_ssid_label = nullptr;
  _wifi_ip_label = nullptr;
  for (int i = 0; i < 4; i++) _conn_bars[i] = nullptr;
}

void power_meter_loop() {
  // UI updates are driven by LVGL timers (_refresh_timer, _elapsed_timer).
  // Measurement engine & recording are handled by main loop.
}

const MeasurementSnapshot &power_meter_get_data() {
  return MeasurementEngine::getInstance().getLatest();
}

void power_meter_key_up()   { power_meter_scope_next_ch(); }
void power_meter_key_down() {}
void power_meter_key_enter() {
  if (_scope_page) power_meter_scope_hide();
  else power_meter_scope_show();
}
void power_meter_key_back()  { if (_scope_page) power_meter_scope_hide(); }

// ©¤©¤ Scope mode ©¤©¤
static void scope_timer_cb(lv_timer_t *) {
  if (!_scope_chart) return;
  auto &s = MeasurementEngine::getInstance().getLatest().channels[_scope_ch];
  lv_chart_set_next_value(_scope_chart, _scope_ser_v, s.bus_voltage_V);
  lv_chart_set_next_value(_scope_chart, _scope_ser_a, s.current_mA / 1000.0f);
  static int sc=0;
  if(++sc%20==0) Serial.printf("[SCOPE] CH%d V=%.2f A=%.3f\n", _scope_ch+1, s.bus_voltage_V, s.current_mA/1000.0f);
  if (_scope_val_label) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.2fV  %.3fA  %.2fW",
      s.bus_voltage_V, s.current_mA/1000.0f, s.power_mW/1000.0f);
    lv_label_set_text(_scope_val_label, buf);
  }
}

void power_meter_scope_show() {
  if (_scope_page) return;
  MeasurementEngine::getInstance().setFastMode(_scope_ch);
  _scope_page = lv_obj_create(lv_scr_act());
  lv_obj_set_size(_scope_page, 320, 170);
  lv_obj_set_style_bg_color(_scope_page, lv_color_hex(0x050505), 0);
  lv_obj_set_style_border_width(_scope_page, 0, 0);
  lv_obj_set_style_pad_all(_scope_page, 0, 0);

  // Channel label
  char lbl[8]; snprintf(lbl, 8, "CH%d", _scope_ch+1);
  lv_obj_t *ch_label = lv_label_create(_scope_page);
  lv_label_set_text(ch_label, lbl);
  lv_obj_set_style_text_color(ch_label, lv_color_hex(0xE27005), 0);
  lv_obj_set_style_text_font(ch_label, &lv_font_montserrat_14, 0);
  lv_obj_align(ch_label, LV_ALIGN_TOP_LEFT, 4, 2);

  _scope_chart = lv_chart_create(_scope_page);
  lv_obj_set_size(_scope_chart, 310, 140);
  lv_obj_align(_scope_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_chart_set_type(_scope_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(_scope_chart, 500);
  lv_chart_set_range(_scope_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 10);
  lv_chart_set_range(_scope_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 5);
  lv_chart_set_div_line_count(_scope_chart, 5, 4);

  _scope_ser_v = lv_chart_add_series(_scope_chart, lv_color_hex(0xE27005), LV_CHART_AXIS_PRIMARY_Y);
  _scope_ser_a = lv_chart_add_series(_scope_chart, lv_color_hex(0x3CB84C), LV_CHART_AXIS_SECONDARY_Y);

  // Real-time values
  _scope_val_label = lv_label_create(_scope_page);
  lv_obj_set_style_text_color(_scope_val_label, lv_color_hex(0xE0E0E0), 0);
  lv_obj_set_style_text_font(_scope_val_label, &lv_font_montserrat_14, 0);
  lv_obj_align(_scope_val_label, LV_ALIGN_TOP_RIGHT, -4, 2);

  _scope_timer = lv_timer_create(scope_timer_cb, 10, nullptr);
}

void power_meter_scope_hide() {
  MeasurementEngine::getInstance().clearFastMode();
  if (_scope_timer) { lv_timer_del(_scope_timer); _scope_timer = nullptr; }
  if (_scope_page) { lv_obj_del(_scope_page); _scope_page = nullptr; _scope_chart = nullptr; }
}

void power_meter_scope_next_ch() {
  _scope_ch = (_scope_ch + 1) % 4;
  if (_scope_page) {
    power_meter_scope_hide();
    power_meter_scope_show();
  }
}
