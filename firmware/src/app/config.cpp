#include "config.h"
#include <Preferences.h>

DeviceSettings &DeviceSettings::getInstance() {
  static DeviceSettings instance;
  return instance;
}

DeviceSettings::DeviceSettings()
    : _sample_interval_ms(1000), _temp_unit('C'), _tz_offset(8), _rotation(0) {
  memset(_device_name, 0, sizeof(_device_name));
  memset(_wifi_ssid, 0, sizeof(_wifi_ssid));
  memset(_wifi_password, 0, sizeof(_wifi_password));
  memset(_ap_ssid, 0, sizeof(_ap_ssid));
  memset(_ap_password, 0, sizeof(_ap_password));

  // Defaults
  strcpy(_device_name, "PowerMeter");
  strcpy(_ap_ssid, "PowerMeter-4CH");
  strcpy(_ap_password, "12345678");
}

void DeviceSettings::begin() {
  load();
}

void DeviceSettings::load() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) {
    Serial.println("[SET] NVS open failed, using defaults");
    return;
  }

  String s;

  s = prefs.getString("dev_name", "");
  if (!s.isEmpty()) {
    strncpy(_device_name, s.c_str(), sizeof(_device_name) - 1);
  }

  s = prefs.getString("ap_ssid", "");
  if (!s.isEmpty()) {
    strncpy(_ap_ssid, s.c_str(), sizeof(_ap_ssid) - 1);
  }

  s = prefs.getString("ap_pass", "");
  if (!s.isEmpty()) {
    strncpy(_ap_password, s.c_str(), sizeof(_ap_password) - 1);
  }

  s = prefs.getString("wifi_ssid", "");
  if (!s.isEmpty()) {
    strncpy(_wifi_ssid, s.c_str(), sizeof(_wifi_ssid) - 1);
  }

  s = prefs.getString("wifi_pass", "");
  if (!s.isEmpty()) {
    strncpy(_wifi_password, s.c_str(), sizeof(_wifi_password) - 1);
  }

  for (int i = 0; i < 4; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ch%d_name", i);
    prefs.getString(key, "");  // legacy key — no longer stored, ignore
  }

  _sample_interval_ms = prefs.getUInt("sample_ms", 1000);
  _temp_unit = prefs.getChar("temp_unit", 'C');
  _tz_offset = prefs.getChar("tz_offset", 8);
  _rotation  = prefs.getUShort("rotation", 0);
  _amb_temp_offset = prefs.getFloat("amb_toff", 0);

  // Stop conditions: single blob key (must read BEFORE prefs.end())
  size_t len = prefs.getBytes("stop_cond", _stop, sizeof(_stop));
  if (len == 0) {
    Serial.println("[SET] no stop_cond in NVS — using defaults (all enabled)");
  } else {
    Serial.println("[SET] stop conditions loaded from NVS");
  }

  prefs.end();

  Serial.println("[SET] Settings loaded from NVS");
  Serial.printf("  Device: %s\n", _device_name);
  Serial.printf("  AP: %s / %s\n", _ap_ssid, _ap_password);
  Serial.printf("  Sample interval: %lu ms\n", _sample_interval_ms);
  Serial.printf("  Temp unit: %c\n", _temp_unit);
}

void DeviceSettings::save() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("[SET] NVS open for write failed!");
    return;
  }

  prefs.putString("dev_name", _device_name);
  prefs.putString("ap_ssid", _ap_ssid);
  prefs.putString("ap_pass", _ap_password);
  prefs.putString("wifi_ssid", _wifi_ssid);
  prefs.putString("wifi_pass", _wifi_password);

  for (int i = 0; i < 4; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ch%d_name", i);
    prefs.remove(key);  // clean up legacy channel names
  }

  prefs.putUInt("sample_ms", _sample_interval_ms);
  prefs.putChar("temp_unit", _temp_unit);
  prefs.putChar("tz_offset", _tz_offset);
  prefs.putUShort("rotation", _rotation);
  prefs.putFloat("amb_toff", _amb_temp_offset);

  // Stop conditions: single blob key
  prefs.putBytes("stop_cond", _stop, sizeof(_stop));

  prefs.end();

  Serial.println("[SET] Settings saved to NVS");
}

// ---- Setters with immediate NVS write ----

void DeviceSettings::setDeviceName(const char *name) {
  strncpy(_device_name, name, sizeof(_device_name) - 1);
  _device_name[sizeof(_device_name) - 1] = '\0';
}

const char *DeviceSettings::channelName(int ch) const {
  static const char *names[4] = {"CH1", "CH2", "CH3", "CH4"};
  if (ch < 0 || ch > 3) return "CH?";
  return names[ch];
}

void DeviceSettings::setWiFi(const char *ssid, const char *password) {
  strncpy(_wifi_ssid, ssid, sizeof(_wifi_ssid) - 1);
  _wifi_ssid[sizeof(_wifi_ssid) - 1] = '\0';
  strncpy(_wifi_password, password, sizeof(_wifi_password) - 1);
  _wifi_password[sizeof(_wifi_password) - 1] = '\0';
}

void DeviceSettings::setAPPassword(const char *pass) {
  strncpy(_ap_password, pass, sizeof(_ap_password) - 1);
  _ap_password[sizeof(_ap_password) - 1] = '\0';
}

void DeviceSettings::setAPSSID(const char *ssid) {
  strncpy(_ap_ssid, ssid, sizeof(_ap_ssid) - 1);
  _ap_ssid[sizeof(_ap_ssid) - 1] = '\0';
}

void DeviceSettings::setSampleIntervalMs(uint32_t ms) {
  if (ms < 100) ms = 100;
  if (ms > 60000) ms = 60000;
  _sample_interval_ms = ms;
}

void DeviceSettings::setTempUnit(char unit) {
  if (unit == 'F' || unit == 'f') _temp_unit = 'F';
  else _temp_unit = 'C';
}

void DeviceSettings::setTzOffset(int8_t off) {
  if (off < -12) off = -12;
  if (off > 14) off = 14;
  _tz_offset = off;
}

void DeviceSettings::setRotation(uint16_t r) {
  if (r != 180) r = 0;
  _rotation = r;
}

void DeviceSettings::setStopCond(int ch, bool en, float v_thresh, float mA_thresh, uint16_t max_min, bool falling) {
  if (ch < 0 || ch > 3) return;
  _stop[ch].enabled = en;
  _stop[ch].voltage_threshold_V = v_thresh;
  _stop[ch].current_threshold_mA = mA_thresh;
  _stop[ch].max_duration_min = max_min;
  _stop[ch].falling_edge = falling;
}

void DeviceSettings::setStopArmed(int ch, bool armed_V, bool armed_mA) {
  if (ch < 0 || ch > 3) return;
  _stop[ch].armed_V = armed_V;
  _stop[ch].armed_mA = armed_mA;
}


