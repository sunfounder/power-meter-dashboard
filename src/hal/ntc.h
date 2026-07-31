#pragma once

#include <Arduino.h>

/**
 * B3950 10K NTC Thermistor Temperature Conversion
 *
 * Converts ADC voltage readings from an NTC voltage divider to temperature.
 *
 * Circuit: 3.3V —— [10K pull-up] —— NTC —— GND
 * ADC reads the voltage at the junction (across NTC).
 *
 * B-parameter equation:
 *   1/T = 1/T0 + (1/B) × ln(R/R0)
 *   where R = R_pullup × V_ntc / (V_ref - V_ntc)
 */

class NTC_B3950 {
public:
  /**
   * @param r_series    Series/pull-up resistor value (ohms), default 10K
   * @param r_nominal   NTC resistance at nominal temp (ohms), default 10K
   * @param t_nominal   Nominal temperature (Celsius), default 25
   * @param b_value     B-parameter, default 3950
   * @param v_ref       Reference/supply voltage, default 3.3V
   */
  NTC_B3950(float r_series = 10000.0f,
            float r_nominal = 10000.0f,
            float t_nominal = 25.0f,
            float b_value = 3950.0f,
            float v_ref = 3.3f);

  /**
   * Convert ADC voltage to temperature.
   * @param voltage  Voltage across the NTC (from ADC)
   * @return Temperature in Celsius
   */
  float voltageToTemp(float voltage);

  /**
   * Convert directly from ADC raw value to temperature.
   * @param adc_raw    Raw 16-bit ADC value (assumes ADS1115 ±2.048V gain)
   * @param adc_max    ADC max count (32767 for 16-bit signed)
   * @param v_ref      ADC reference voltage (2.048 for ADS1115 ±2.048V)
   * @return Temperature in Celsius
   */
  float adcToTemp(int16_t adc_raw, float v_ref = 2.048f, int16_t adc_max = 32767);

  /**
   * Convert voltage across NTC to resistance.
   */
  float voltageToResistance(float voltage);

  /**
   * Convert resistance to temperature using B-parameter equation.
   */
  float resistanceToTemp(float resistance);

private:
  float _r_series;
  float _r_nominal;
  float _t_nominal;  // in Celsius
  float _b_value;
  float _v_ref;
};
