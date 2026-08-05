#include "measure.h"
#include "record.h"
#include "config.h"
#include "../net/log.h"
#include "../hal/pin_config.h"

// Auto-stop: require N consecutive below-threshold samples (glitch filter)
#define AUTO_STOP_SAMPLES 10

MeasurementEngine &MeasurementEngine::getInstance() {
  static MeasurementEngine instance;
  return instance;
}

MeasurementEngine::MeasurementEngine()
    : _ina226{
        INA226(INA226_ADDR_CH1, INA226_SHUNT_MOHM),
        INA226(INA226_ADDR_CH2, INA226_SHUNT_MOHM),
        INA226(INA226_ADDR_CH3, INA226_SHUNT_MOHM),
        INA226(INA226_ADDR_CH4, INA226_SHUNT_MOHM)
      },
      _ads1115(ADS1115_ADDR),
      _ntc(NTC_SERIES_RESISTOR, NTC_NOMINAL_RESISTANCE,
           NTC_NOMINAL_TEMP, NTC_B_VALUE, 3.3f),
      _ch_mgr(),
      _last_sample_ms(0), _sample_interval_ms(1000), _sensors_ok(false)
{
  memset(&_snapshot, 0, sizeof(_snapshot));
  memset(_alarm_configs, 0, sizeof(_alarm_configs));
  memset(_connected, 0, sizeof(_connected));
}

bool MeasurementEngine::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // Initialize channel controls (EN pins)
  _ch_mgr.begin();

  // Initialize ADS1115
  if (!_ads1115.begin()) {
    Serial.println("[ME] ADS1115 not found!");
  } else {
    Serial.println("[ME] ADS1115 OK");
  }

  // Initialize INA226 chips
  int ina_ok = 0;
  for (int i = 0; i < 4; i++) {
    if (_ina226[i].begin()) {
      _connected[i] = true;
      WebLog::getInstance().log("INA226 CH%d (0x%02X) OK", i + 1, _ina226[i].getAddr());
      ina_ok++;
    } else {
      WebLog::getInstance().log("INA226 CH%d (0x%02X) NOT FOUND", i + 1, _ina226[i].getAddr());
    }
  }

  _sensors_ok = (ina_ok >= 1);
  WebLog::getInstance().log("Sensors: %d/4 INA226, ADS1115 %s",
    ina_ok, _ads1115.isConnected() ? "OK" : "NONE");

  // Print I2C bus scan for diagnostics (only known addresses)
  Serial.println("[ME] I2C bus scan:");
  static const uint8_t known_addrs[] = {0x40, 0x41, 0x44, 0x45, 0x48};
  for (uint8_t addr : known_addrs) {
    Wire.beginTransmission(addr);
    bool found = (Wire.endTransmission() == 0);
    Serial.printf("  0x%02X: %s\n", addr, found ? "OK" : "NONE");
  }
  return _sensors_ok;
}

void MeasurementEngine::update() {
  uint32_t now = millis();
  auto &recorder = DataRecorder::getInstance();

  // Fast scope mode: single channel, 10ms, no temp, no record
  if (_fast_ch >= 0) {
    if (now - _last_sample_ms >= 10) {
      _last_sample_ms = now;
      auto m = _ina226[_fast_ch].readAll();
      _snapshot.timestamp_ms = time(nullptr);
      _snapshot.channels[_fast_ch].bus_voltage_V = m.bus_voltage_V;
      _snapshot.channels[_fast_ch].current_mA    = m.current_mA;
      _snapshot.channels[_fast_ch].power_mW      = m.power_mW;
      _snapshot.channels[_fast_ch].connected     = true;
    }
    return;
  }

  if (recorder.isAnyRecording()) {
    if (now - _last_sample_ms >= _sample_interval_ms) {
      _last_sample_ms = now;
      _sampleAll();
      recorder.appendSample(_snapshot);
      checkAlarms();
      checkAutoStop();
    }
  } else {
    if (now - _last_sample_ms >= 1000) {
      _last_sample_ms = now;
      _sampleAll();
    }
  }
}

void MeasurementEngine::setFastMode(int ch) {
  if (ch < 0 || ch > 3) return;
  _fast_ch = ch;
  _last_sample_ms = 0;
}
void MeasurementEngine::clearFastMode() {
  _fast_ch = -1;
  _last_sample_ms = 0;
}

void MeasurementEngine::_sampleAll() {
  _snapshot.timestamp_ms = time(nullptr);  // real UTC seconds

  static float last_temp[4] = {0,0,0,0};
  static uint8_t bad_cnt[4] = {0,0,0,0};
  for (int i = 0; i < 4; i++) {
    if (!_connected[i]) {
      // Channel not detected at boot — no NTC reading needed
      _snapshot.channels[i].channel_temp_C = -999.0f;
      _snapshot.channels[i].connected = false;
      continue;
    }

    // Read NTC with glitch filter (3 consecutive fails = real open)
    float ntc_v = _ads1115.readVoltage(i);
    float t = _ntc.voltageToTemp(ntc_v);
    if (t < -100) {
      if (++bad_cnt[i] < 3 && last_temp[i] > -100) t = last_temp[i];
    } else {
      bad_cnt[i] = 0;
      last_temp[i] = t;
    }
    _snapshot.channels[i].channel_temp_C = t;

    auto m = _ina226[i].readAll();
    if (m.bus_voltage_V < 0.001f && m.current_mA < 0.01f) continue;
    _snapshot.channels[i].bus_voltage_V    = m.bus_voltage_V;
    _snapshot.channels[i].shunt_voltage_mV = m.shunt_voltage_mV;
    _snapshot.channels[i].current_mA       = m.current_mA;
    _snapshot.channels[i].power_mW         = m.power_mW;
    _snapshot.channels[i].timestamp_ms     = _snapshot.timestamp_ms;
    _snapshot.channels[i].connected        = true;
  }

  // Read ambient temperature
  _snapshot.env.ambient_temp_C = _readAmbientTemp();
}

float MeasurementEngine::_readAmbientTemp() {
  analogSetAttenuation(ADC_11db);
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(PIN_AMB_NTC);
    delay(1);
  }
  float raw_avg = (float)sum / 16.0f;
  float voltage = raw_avg * 3.3f / 4095.0f;
  float temp = _ntc.voltageToTemp(voltage);
  // Hold last good value on transient glitches (3 consecutive fails = real open)
  static float last_amb = 0;
  static uint8_t amb_bad = 0;
  if (temp < -100) {
    if (++amb_bad < 3 && last_amb > -100) temp = last_amb;
  } else {
    amb_bad = 0;
    last_amb = temp;
  }
  return temp + DeviceSettings::getInstance().ambTempOffset();
}

bool MeasurementEngine::isINA226Connected(int ch) const {
  if (ch < 0 || ch > 3) return false;
  return const_cast<INA226&>(_ina226[ch]).isConnected();
}

bool MeasurementEngine::isADS1115Connected() const {
  return const_cast<ADS1115&>(_ads1115).isConnected();
}

bool MeasurementEngine::isAnyRecording() const {
  return DataRecorder::getInstance().isAnyRecording();
}

bool MeasurementEngine::isChannelRecording(int ch) const {
  return DataRecorder::getInstance().isChannelRecording(ch);
}

void MeasurementEngine::checkAlarms() {
  for (int ch = 0; ch < 4; ch++) {
    auto &cfg = _alarm_configs[ch];
    auto &sample = _snapshot.channels[ch];

    for (int c = 0; c < 2; c++) {
      auto &alarm = cfg.conditions[c];
      if (!alarm.enabled || alarm.triggered) continue;

      float value = 0;
      switch (alarm.condition) {
        case AlarmConfig::COND_CURRENT_BELOW:
        case AlarmConfig::COND_CURRENT_ABOVE:
          value = sample.current_mA; break;
        case AlarmConfig::COND_VOLTAGE_BELOW:
        case AlarmConfig::COND_VOLTAGE_ABOVE:
          value = sample.bus_voltage_V; break;
        case AlarmConfig::COND_TEMP_BELOW:
        case AlarmConfig::COND_TEMP_ABOVE:
          value = sample.channel_temp_C; break;
        case AlarmConfig::COND_POWER_BELOW:
        case AlarmConfig::COND_POWER_ABOVE:
          value = sample.power_mW; break;
        default: continue;
      }

      bool condition_met = false;
      switch (alarm.condition) {
        case AlarmConfig::COND_CURRENT_BELOW:
        case AlarmConfig::COND_VOLTAGE_BELOW:
        case AlarmConfig::COND_TEMP_BELOW:
        case AlarmConfig::COND_POWER_BELOW:
          condition_met = (value < alarm.threshold); break;
        case AlarmConfig::COND_CURRENT_ABOVE:
        case AlarmConfig::COND_VOLTAGE_ABOVE:
        case AlarmConfig::COND_TEMP_ABOVE:
        case AlarmConfig::COND_POWER_ABOVE:
          condition_met = (value > alarm.threshold); break;
        default: break;
      }

      if (condition_met) {
        if (alarm.triggered_at == 0) {
          alarm.triggered_at = millis();
        } else if (millis() - alarm.triggered_at >= alarm.duration_ms) {
          alarm.triggered = true;
          Serial.printf("[ALARM] CH%d condition %d triggered!\n", ch + 1, c);
        }
      } else {
        alarm.triggered_at = 0;
      }
    }
  }
}

void MeasurementEngine::checkAutoStop() {
  auto &rec = DataRecorder::getInstance();
  auto &cfg = DeviceSettings::getInstance();
  // Consecutive below-threshold samples before stopping (glitch filter)
  static uint8_t below_cnt[4] = {0,0,0,0};
  for (int ch = 0; ch < 4; ch++) {
    if (!rec.isChannelRecording(ch)) continue;
    auto &sc = cfg.stopCond(ch);
    // DIAG: log every 30th check while recording
    {
      static uint32_t diag_cnt = 0;
      if ((diag_cnt++ % 30) == 0) {
        auto &s = _snapshot.channels[ch];
        Serial.printf("[ASTOP] CH%d en=%d Vth=%.2f mAth=%.1f min=%u fall=%d armedV=%d armedmA=%d | V=%.2f mA=%.1f below_cnt=%u\n",
                      ch+1, sc.enabled, sc.voltage_threshold_V, sc.current_threshold_mA,
                      sc.max_duration_min, sc.falling_edge, sc.armed_V, sc.armed_mA,
                      s.bus_voltage_V, s.current_mA, below_cnt[ch]);
      }
    }
    if (!sc.enabled) continue;
    uint32_t elapsed_s = (millis() - rec.channelState(ch).start_time) / 1000;
    if (sc.max_duration_min > 0 && elapsed_s >= sc.max_duration_min * 60UL) {
      WebLog::getInstance().log("AUTO-STOP CH%d: max duration %d min", ch+1, sc.max_duration_min);
      rec.stopChannel(ch); continue;
    }
    auto &s = _snapshot.channels[ch];
    bool below = false;
    if (sc.voltage_threshold_V > 0) {
      if (sc.falling_edge) {
        if (!sc.armed_V && s.bus_voltage_V > sc.voltage_threshold_V * 1.2f) cfg.setStopArmed(ch, true, sc.armed_mA);
        if (sc.armed_V && s.bus_voltage_V < sc.voltage_threshold_V) below = true;
      } else { if (s.bus_voltage_V < sc.voltage_threshold_V) below = true; }
    }
    if (sc.current_threshold_mA > 0) {
      if (sc.falling_edge) {
        if (!sc.armed_mA && s.current_mA > sc.current_threshold_mA * 1.2f) cfg.setStopArmed(ch, sc.armed_V, true);
        if (sc.armed_mA && s.current_mA < sc.current_threshold_mA) below = true;
      } else { if (s.current_mA < sc.current_threshold_mA) below = true; }
    }
    if (below) {
      // Require N consecutive low samples before auto-stop
      if (++below_cnt[ch] >= AUTO_STOP_SAMPLES) {
        WebLog::getInstance().log("AUTO-STOP CH%d: condition held %d samples", ch+1, below_cnt[ch]);
        rec.stopChannel(ch);
      }
    } else {
      below_cnt[ch] = 0;
    }
  }
}
