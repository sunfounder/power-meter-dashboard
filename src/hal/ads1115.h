#pragma once

#include <Arduino.h>
#include <Wire.h>

/**
 * ADS1115 16-bit 4-channel ADC Driver
 *
 * Used to read 4× B3950 NTC thermistors (one per power channel).
 * Single-shot mode: trigger a read on each channel as needed.
 */

class ADS1115 {
public:
  ADS1115(uint8_t addr = 0x48);

  bool begin(TwoWire &wire = Wire);
  bool isConnected();

  // Read raw ADC value from a channel (0-3, single-ended)
  int16_t readChannel(uint8_t channel);

  // Read voltage on a channel (in volts)
  float readVoltage(uint8_t channel);

  // Read all 4 channels at once (raw values)
  void readAllChannels(int16_t values[4]);

  // Read all 4 channels at once (voltage values)
  void readAllVoltages(float voltages[4]);

  // Configure gain (PGA)
  enum Gain {
    GAIN_6_144V = 0,  // ±6.144V (default)
    GAIN_4_096V = 1,  // ±4.096V
    GAIN_2_048V = 2,  // ±2.048V
    GAIN_1_024V = 3,  // ±1.024V
    GAIN_0_512V = 4,  // ±0.512V
    GAIN_0_256V = 5   // ±0.256V
  };
  void setGain(Gain gain);

  // Set data rate (samples per second)
  enum DataRate {
    RATE_8_SPS   = 0,
    RATE_16_SPS  = 1,
    RATE_32_SPS  = 2,
    RATE_64_SPS  = 3,
    RATE_128_SPS = 4,
    RATE_250_SPS = 5,
    RATE_475_SPS = 6,
    RATE_860_SPS = 7
  };
  void setDataRate(DataRate rate);

  // Get the voltage LSB for the current gain setting
  float getVoltageLSB() const;

private:
  uint8_t _addr;
  Gain _gain;
  float _voltage_LSB;
  TwoWire *_wire;
  int16_t _last_raw[4] = {0,0,0,0};
  float _last_voltage[4] = {0,0,0,0};

  void writeRegister(uint8_t reg, uint16_t value);
  uint16_t readRegister(uint8_t reg);
  uint16_t readADC_SingleEnded(uint8_t channel);

  // Register addresses
  static constexpr uint8_t REG_CONVERSION = 0x00;
  static constexpr uint8_t REG_CONFIG     = 0x01;
  static constexpr uint8_t REG_THRESH_LO  = 0x02;
  static constexpr uint8_t REG_THRESH_HI  = 0x03;

  // Config register bits
  static constexpr uint16_t CONFIG_OS_SINGLE = 0x8000;
  static constexpr uint16_t CONFIG_MUX_SINGLE_0 = 0x4000; // AIN0-GND
  static constexpr uint16_t CONFIG_MUX_SINGLE_1 = 0x5000; // AIN1-GND
  static constexpr uint16_t CONFIG_MUX_SINGLE_2 = 0x6000; // AIN2-GND
  static constexpr uint16_t CONFIG_MUX_SINGLE_3 = 0x7000; // AIN3-GND
};
