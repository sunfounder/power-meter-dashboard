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
  // Per-channel auto-stop conditions
  struct StopCond {
    bool  enabled = false;
    float voltage_threshold_V  = 0;
    float current_threshold_mA = 0;
    uint16_t max_duration_min  = 0;
    bool  falling_edge = true;
    bool  armed_V  = false;
    bool  armed_mA = false;
    float peak_V  = 0;
    float peak_mA = 0;
  };

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

  // Display rotation (0/90/180/270)
  uint16_t rotation() const { return _rotation; }
  void setRotation(uint16_t r);

  // Ambient temp calibration offset
  float ambTempOffset() const { return _amb_temp_offset; }
  void setAmbTempOffset(float v) { _amb_temp_offset = v; }

  // Auto-stop conditions (per channel)
  const StopCond &stopCond(int ch) const { return _stop[ch]; }
  void setStopCond(int ch, bool enabled, float v_thresh, float mA_thresh, uint16_t max_min, bool falling);
  void setStopArmed(int ch, bool armed_V, bool armed_mA);

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
  int8_t _tz_offset;
  uint16_t _rotation;
  float _amb_temp_offset;  // 0, 90, 180, 270

  StopCond _stop[4];
};
