#pragma once

#include <Arduino.h>
#include <Wire.h>

/**
 * INA226 Current/Power Monitor Driver
 *
 * Features:
 * - Bus voltage, shunt voltage, current, power measurement
 * - Programmable averaging and conversion time
 * - 4 I2C addresses supported (0x40-0x4F via A0/A1 pins)
 */

class INA226 {
public:
  INA226(uint8_t addr, float shunt_mohm = 10.0f);

  bool begin(TwoWire &wire = Wire);
  bool isConnected();

  // Configure measurement parameters
  void setAveraging(uint8_t avg);       // 0-7 (1,4,16,64,128,256,512,1024)
  void setBusConversionTime(uint8_t ct); // 0-7 (140us-8.244ms)
  void setShuntConversionTime(uint8_t ct);
  void setMode(uint8_t mode);            // 0-7 (power down, shunt, bus, shunt+bus trig/cont)

  // Default configuration: 1.1ms conversion, 1x averaging, continuous shunt+bus
  void configureDefault();

  // Calibration (call after setting shunt resistor value)
  void calibrate();

  // Read measurements (returns raw values)
  int16_t readShuntVoltageRaw();
  int16_t readBusVoltageRaw();
  int16_t readCurrentRaw();
  int16_t readPowerRaw();

  // Read measurements (returns converted values)
  float readShuntVoltage_mV();
  float readBusVoltage_V();
  float readCurrent_mA();
  float readPower_mW();

  // Combined read (single I2C transaction for efficiency)
  struct Measurement {
    float bus_voltage_V;
    float shunt_voltage_mV;
    float current_mA;
    float power_mW;
  };
  Measurement readAll();

  // Set shunt resistor value (milliohms)
  void setShuntResistance(float mohm) { _shunt_mohm = mohm; }

  // Device info
  uint8_t getAddr() const { return _addr; }
  uint16_t readManufacturerID();  // Should be 0x5449
  uint16_t readDieID();           // Should be 0x2260

private:
  uint8_t _addr;
  float _shunt_mohm;
  float _current_LSB;
  TwoWire *_wire;
  uint8_t dbg_cnt;  // per-instance debug counter

  void writeRegister(uint8_t reg, uint16_t value);
  uint16_t readRegister(uint8_t reg);

  // INA226 registers
  static constexpr uint8_t REG_CONFIG    = 0x00;
  static constexpr uint8_t REG_SHUNT_V   = 0x01;
  static constexpr uint8_t REG_BUS_V     = 0x02;
  static constexpr uint8_t REG_POWER     = 0x03;
  static constexpr uint8_t REG_CURRENT   = 0x04;
  static constexpr uint8_t REG_CALIB     = 0x05;
  static constexpr uint8_t REG_MASK      = 0x06;
  static constexpr uint8_t REG_ALERT     = 0x07;
  static constexpr uint8_t REG_MFR_ID    = 0xFE;
  static constexpr uint8_t REG_DIE_ID    = 0xFF;

  // Default calibration
  static constexpr float MAX_CURRENT_A   = 16.384f;  // 81.92mV / 5m¦¸ = 16.384A (exact full-scale)
  static constexpr float SHUNT_V_FULL    = 0.08192f; // Â±81.92mV full scale
};
