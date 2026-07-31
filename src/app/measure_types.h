#pragma once

#include <Arduino.h>

/**
 * Single measurement sample for one channel
 */
struct ChannelSample {
  float bus_voltage_V;
  float shunt_voltage_mV;
  float current_mA;
  float power_mW;
  float channel_temp_C;
  uint32_t timestamp_ms;
  bool connected;          // INA226 detected
};

/**
 * Environmental data (one per sampling period)
 */
struct EnvSample {
  float ambient_temp_C;     // Ambient NTC temperature
};

/**
 * Complete snapshot of all channels + environment at one moment
 */
struct MeasurementSnapshot {
  uint32_t timestamp_ms;    // Absolute timestamp (millis)
  ChannelSample channels[4];
  EnvSample env;
};

/**
 * Per-channel recording state (4 independent sessions)
 */
struct ChannelRecordingState {
  char name[48];            // User-defined test name
  uint32_t start_time;      // millis when recording started
  bool active;              // Is this channel recording?
  uint32_t sample_count;    // Number of samples recorded
};

/**
 * Test session metadata (global, for backward compat / aggregate info)
 */
struct TestSession {
  char name[64];            // User-defined test name
  uint32_t start_time;      // Unix timestamp when recording started
  bool active;              // Is recording in progress? (any channel)
  uint32_t sample_count;    // Number of samples recorded
  uint32_t sample_interval_ms; // Interval between samples (default 1000ms)
};

/**
 * Alarm / test-end trigger configuration per channel
 */
struct AlarmConfig {
  enum Condition {
    COND_NONE = 0,
    COND_CURRENT_BELOW,     // Current < threshold
    COND_CURRENT_ABOVE,
    COND_VOLTAGE_BELOW,
    COND_VOLTAGE_ABOVE,
    COND_TEMP_BELOW,
    COND_TEMP_ABOVE,
    COND_POWER_BELOW,
    COND_POWER_ABOVE,
  };

  bool enabled;
  Condition condition;
  float threshold;
  unsigned long duration_ms; // Condition must hold this long
  unsigned long triggered_at; // When condition first met (0 if not triggered)
  bool triggered;
};

/**
 * Per-channel alarm configs (up to 2 conditions per channel)
 */
struct ChannelAlarmConfig {
  AlarmConfig conditions[2];
};

/**
 * Global configuration (persisted to NVS)
 */
struct GlobalConfig {
  char wifi_ssid[32];
  char wifi_password[64];
  bool wifi_ap_mode;        // true = AP, false = STA
  uint32_t sample_interval_ms;
  AlarmConfig global_alarm;  // Ambient temp alarm etc.
};
