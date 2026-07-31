#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "version.h"
#include "app/measure.h"
#include "app/record.h"
#include "app/config.h"
#include "app/screen.h"
#include "net/server.h"
#include "net/log.h"
#include "hal/buzzer.h"
#include "hal/pin_config.h"
#include "hal/display.h"

static Buzzer buzzer(PIN_BUZZER);
Buzzer &g_buzzer = buzzer;

static uint32_t last_web_broadcast = 0;
static uint32_t last_lvgl_tick = 0;
static uint32_t last_ntp_check = 0;
static lv_display_t *_lv_disp = nullptr;
lv_display_t *g_lv_disp = nullptr; // for flush-ready callback

// LVGL flush callback — defer lv_display_flush_ready to I80 completion
static void lvgl_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
  esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(d);
  display_draw_bitmap(panel, area->x1, area->y1, area->x2, area->y2, px_map);
  // do NOT call lv_display_flush_ready here — I80 driver will via on_flush_ready
}
static void on_i80_flush_ready(void *) {
  if (_lv_disp) lv_display_flush_ready(_lv_disp);
}

static void show_splash_screen() {
  lv_obj_t *splash = lv_obj_create(lv_screen_active());
  lv_obj_set_size(splash, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_bg_color(splash, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(splash, 0, 0);
  lv_obj_set_style_radius(splash, 0, 0);
  lv_obj_set_flex_flow(splash, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(splash, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *name_label = lv_label_create(splash);
  lv_label_set_text(name_label, PROJECT_NAME);
  lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
  lv_obj_t *ver_label = lv_label_create(splash);
  lv_label_set_text(ver_label, PROJECT_VERSION);
  lv_obj_set_style_text_color(ver_label, lv_color_hex(0x888888), 0);
  lv_refr_now(NULL);
  display_backlight_fade_in(500);
  delay(1500);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_refr_now(NULL);
  lv_obj_del(splash);
  lv_refr_now(NULL);
}

static void checkAlarms() {
  static bool was_triggered[4] = {false};
  for (int i = 0; i < 4; i++) {
    auto &cfg = MeasurementEngine::getInstance().alarmConfig(i);
    bool any = false;
    for (int c = 0; c < 2; c++) {
      if (cfg.conditions[c].enabled && cfg.conditions[c].triggered) { any = true; break; }
    }
    if (any && !was_triggered[i]) {
      buzzer.playWarning();
      char msg[48];
      snprintf(msg, sizeof(msg), "CH%d alarm triggered!", i + 1);
      PowerMeterWebServer::getInstance().notifyAlarm(i, msg);
    }
    was_triggered[i] = any;
  }
}

static void printAPDiagnostics() {
  IPAddress ip = WiFi.softAPIP();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("[INIT] AP MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("[INIT] AP IP:  %s\n", ip.toString().c_str());
}

/* ──────────────── Setup ──────────────── */

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("========================================");
  Serial.printf("  %s v%s\n", PRODUCT_NAME, FIRMWARE_VERSION);
  Serial.println("  4-Channel Power Meter");
  Serial.println("  ESP32-S3 / T-Display-S3");
  Serial.println("========================================");

  buzzer.begin();
  WebLog::getInstance().begin();
  DeviceSettings::getInstance().begin();

  if (!MeasurementEngine::getInstance().begin())
    Serial.println("[INIT] WARNING: Some sensors not found");
  else
    Serial.println("[INIT] All sensors OK");
  MeasurementEngine::getInstance().setSampleInterval(
      DeviceSettings::getInstance().sampleIntervalMs());

  if (!DataRecorder::getInstance().begin())
    Serial.println("[INIT] WARNING: LittleFS mount failed");
  else
    DataRecorder::getInstance().listFiles();

  // ── Step 1: WiFi FIRST ──
  PowerMeterWebServer::getInstance().startAP();
  printAPDiagnostics();

  // ── Step 2: I80 + LVGL + UI ──
  display_init();
  Serial.println("[INIT] I80 done");
  delay(2000);

  lv_init();
  const size_t draw_buf_sz = LCD_H_RES * 60;
  lv_color_t *buf = (lv_color_t *)heap_caps_malloc(
      draw_buf_sz * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_t *d = lv_display_create(LCD_H_RES, LCD_V_RES);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_buffers(d, buf, NULL, draw_buf_sz * sizeof(lv_color_t),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(d, display_get_panel_handle());
  lv_display_set_flush_cb(d, lvgl_flush_cb);
  lv_display_set_rotation(d, (lv_display_rotation_t)(DeviceSettings::getInstance().rotation() / 90));
  _lv_disp = d;
  display_set_on_flush_ready(on_i80_flush_ready);
  g_lv_disp = d;
  lv_group_create();

  show_splash_screen();
  power_meter_init();

  // ── Step 3: Restart WiFi ──
  PowerMeterWebServer::getInstance().startAP();
  PowerMeterWebServer::getInstance().begin();
  printAPDiagnostics();
}

/* ──────────────── Loop ──────────────── */

void loop() {
  uint32_t now = millis();
  lv_tick_inc(now - last_lvgl_tick);
  last_lvgl_tick = now;
  lv_timer_handler();

  MeasurementEngine::getInstance().update();
  buzzer.update();
  PowerMeterWebServer::getInstance().update();

  if (now - last_web_broadcast >= 500) {
    last_web_broadcast = now;
    PowerMeterWebServer::getInstance().broadcastData(
        MeasurementEngine::getInstance().getLatest());
  }

  checkAlarms();

  // NTP retry: if STA connected but time not synced, retry every 30s
  if (now - last_ntp_check > 30000 && WiFi.status() == WL_CONNECTED) {
    last_ntp_check = now;
    time_t t = time(nullptr);
    if (t < 1600000000) { // not synced yet
      int8_t tz = DeviceSettings::getInstance().tzOffset();
      configTime(tz * 3600, 0, "pool.ntp.org", "time.nist.gov");
      Serial.printf("[NTP] Retry sync (UTC%+d)\n", tz);
    }
  }

  delay(5);
}
