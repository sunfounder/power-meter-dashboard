#include "ntc.h"
#include <math.h>

NTC_B3950::NTC_B3950(float r_series, float r_nominal, float t_nominal,
                     float b_value, float v_ref)
    : _r_series(r_series), _r_nominal(r_nominal), _t_nominal(t_nominal),
      _b_value(b_value), _v_ref(v_ref) {}

float NTC_B3950::voltageToResistance(float voltage) {
  // Prevent divide by zero and clamp
  if (voltage <= 0.001f) voltage = 0.001f;
  if (voltage >= _v_ref - 0.001f) voltage = _v_ref - 0.001f;

  // R_ntc = R_series × V_ntc / (V_ref - V_ntc)
  return _r_series * voltage / (_v_ref - voltage);
}

float NTC_B3950::resistanceToTemp(float resistance) {
  if (resistance <= 0) return -999.0f;

  // B-parameter equation:
  // 1/T = 1/T0 + (1/B) × ln(R/R0)
  // T in Kelvin, T0 = _t_nominal + 273.15

  float t0_kelvin = _t_nominal + 273.15f;
  float log_ratio = log(resistance / _r_nominal);

  float inv_t = (1.0f / t0_kelvin) + (1.0f / _b_value) * log_ratio;
  if (inv_t <= 0) return 999.0f;  // Unrealistic

  float temp_kelvin = 1.0f / inv_t;
  return temp_kelvin - 273.15f;
}

float NTC_B3950::voltageToTemp(float voltage) {
  float resistance = voltageToResistance(voltage);
  // Detect open circuit: resistance out of reasonable range
  if (resistance < 50 || resistance > 1000000) {
    Serial.printf("[NTC] disconnected: V=%.2f R=%.0f\n", voltage, resistance);
    return -999.0f;
  }
  return resistanceToTemp(resistance);
}

float NTC_B3950::adcToTemp(int16_t adc_raw, float v_ref, int16_t adc_max) {
  // Convert signed ADC value to voltage
  // For ADS1115, raw value is signed 16-bit with max = ±32767
  float voltage = ((float)adc_raw / (float)adc_max) * v_ref;
  if (voltage < 0) voltage = 0;  // Clamp negative to 0
  return voltageToTemp(voltage);
}
