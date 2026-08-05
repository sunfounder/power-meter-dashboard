#pragma once

#include "measure_types.h"
#include "../hal/ina226.h"
#include "../hal/ads1115.h"
#include "../hal/ntc.h"
#include "../hal/channel.h"

/**
 * Measurement Engine
 *
 * Coordinates all sensors (4× INA226, 1× ADS1115, 1× ambient NTC)
 * to produce regular MeasurementSnapshots.
 *
 * Supports per-channel independent recording via DataRecorder.
 * Sampling runs at configured rate whenever ANY channel is recording;
 * otherwise runs at 1Hz for display.
 */
class MeasurementEngine {
public:
  static MeasurementEngine &getInstance();

  bool begin();
  void update();  // Call in main loop

  // Get latest snapshot
  const MeasurementSnapshot &getLatest() const { return _snapshot; }

  // Sensor health check
  bool isINA226Connected(int ch) const;
  bool isADS1115Connected() const;

  // Recording state (delegates to DataRecorder)
  bool isAnyRecording() const;
  bool isChannelRecording(int ch) const;

  // Channel alarm configs
  ChannelAlarmConfig &alarmConfig(int ch) { return _alarm_configs[ch]; }
  void checkAlarms();
  void checkAutoStop();

  // Get channel manager
  ChannelManager &channels() { return _ch_mgr; }

  // Sample interval
  void setSampleInterval(uint32_t ms) { _sample_interval_ms = ms; }
  uint32_t getSampleInterval() const { return _sample_interval_ms; }

  // Fast scope mode: single channel, no temp, faster rate
  void setFastMode(int ch);
  void clearFastMode();
  bool isFastMode() const { return _fast_ch >= 0; }
  int  fastChannel() const { return _fast_ch; }

private:
  MeasurementEngine();

  INA226 _ina226[4];
  ADS1115 _ads1115;
  NTC_B3950 _ntc;
  ChannelManager _ch_mgr;

  MeasurementSnapshot _snapshot;
  ChannelAlarmConfig _alarm_configs[4];
  bool _connected[4];
  uint32_t _last_conn_check_ms;

  uint32_t _last_sample_ms;
  uint32_t _sample_interval_ms;
  bool _sensors_ok;
  int _fast_ch = -1;  // -1 = normal mode; 0-3 = fast scope channel

  void _sampleAll();
  float _readAmbientTemp();
};
