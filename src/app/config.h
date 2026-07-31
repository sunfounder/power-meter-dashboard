#pragma once

#include <Arduino.h>
#include <Preferences.h>

/**
 * Device Settings — persisted to NVS (ESP32 Preferences)
 *
 * Stores: device name, 4× channel names, WiFi config, AP password,
 *          sample interval, etc.
 */

class DeviceSettings {
public:
  static DeviceSettings &getInstance();

  void begin();
  void load();
  void save();

  // Device identity
  const char *deviceName() const { return _device_name; }
  void setDeviceName(const char *name);

  // Channel names
  const char *channelName(int ch) const;
  void setChannelName(int ch, const char *name);

  // WiFi
  const char *wifiSSID() const { return _wifi_ssid; }
  const char *wifiPassword() const { return _wifi_password; }
  void setWiFi(const char *ssid, const char *password);

  // AP mode
  const char *apPassword() const { return _ap_password; }
  void setAPPassword(const char *pass);
  const char *apSSID() const { return _ap_ssid; }
  void setAPSSID(const char *ssid);

  // Sample interval
  uint32_t sampleIntervalMs() const { return _sample_interval_ms; }
  void setSampleIntervalMs(uint32_t ms);

  // Temperature unit
  char tempUnit() const { return _temp_unit; }
  void setTempUnit(char unit);

  // Timezone offset (hours from UTC, e.g. 8 = UTC+8)
  int8_t tzOffset() const { return _tz_offset; }
  void setTzOffset(int8_t off);

private:
  DeviceSettings();

  static constexpr const char *NVS_NS = "settings";

  char _device_name[32];
  char _channel_names[4][16];
  char _wifi_ssid[32];
  char _wifi_password[64];
  char _ap_ssid[32];
  char _ap_password[32];
  uint32_t _sample_interval_ms;
  char _temp_unit;  // 'C' or 'F'
  int8_t _tz_offset; // hours from UTC, default 8 (China)
};
