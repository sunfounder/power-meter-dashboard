#include "ina226.h"
#include "../net/log.h"

INA226::INA226(uint8_t addr, float shunt_mohm)
    : _addr(addr), _shunt_mohm(shunt_mohm), _current_LSB(0), _wire(nullptr), dbg_cnt(0) {}

bool INA226::begin(TwoWire &wire) {
  _wire = &wire;
  // Retry MFR ID read ¡ª multi-chip I2C bus may need settling
  for (int retry = 0; retry < 5; retry++) {
    uint16_t mfrId = readManufacturerID();
    if (mfrId == 0x5449) {
      configureDefault();
      calibrate();
      WebLog::getInstance().log("INA 0x%02X: OK (shunt=%.0fmohm)", _addr, _shunt_mohm);
      return true;
    }
    if (retry < 4) { delay(10); }
  }
  Serial.printf("[INA] 0x%02X: bad MFR ID, skipping after 5 retries\n", _addr);
  return false;
}

bool INA226::isConnected() {
  if (!_wire) return false;
  _wire->beginTransmission(_addr);
  return _wire->endTransmission() == 0;
}

void INA226::setAveraging(uint8_t avg)       { /* 9-11 bits in config */ }
void INA226::setBusConversionTime(uint8_t ct) { /* 6-8 bits */ }
void INA226::setShuntConversionTime(uint8_t ct) { /* 3-5 bits */ }
void INA226::setMode(uint8_t mode)             { /* 0-2 bits */ }

void INA226::configureDefault() {
  writeRegister(REG_CONFIG, 0x8000);  // reset
  delay(1);

  // AVG=4, VBUSCT=1.1ms, VSHCT=1.1ms, MODE=shunt+bus continuous
  // Bits: 0000 0010 0001 0111 = 0x0217
  writeRegister(REG_CONFIG, 0x0217);
}

void INA226::calibrate() {
  // For 5m¦¸ shunt: MAX_CURRENT = 81.92mV / 5m¦¸ = 16.384A
  // Per datasheet: Current_LSB = MaxExpectedCurrent / 2^15
  // Calibration = 0.00512 / (Current_LSB * Rshunt)
  _current_LSB = MAX_CURRENT_A / 32768.0f;
  float calibration = 0.00512f / (_current_LSB * (_shunt_mohm / 1000.0f));
  uint16_t cal_val = (uint16_t)calibration;
  writeRegister(REG_CALIB, cal_val);

  // Verify by reading back
  uint16_t readback = readRegister(REG_CALIB);
  Serial.printf("[INA] 0x%02X: shunt=%.0fmohm, MAX_I=%.0fA, CAL wrote=%u read=%u\n",
                _addr, _shunt_mohm, MAX_CURRENT_A, cal_val, readback);
  if (readback != cal_val) {
    Serial.printf("[INA] 0x%02X: CAL MISMATCH! Retrying...\n", _addr);
    writeRegister(REG_CALIB, cal_val);
    readback = readRegister(REG_CALIB);
    Serial.printf("[INA] 0x%02X: CAL retry read=%u\n", _addr, readback);
  }
}

int16_t INA226::readShuntVoltageRaw() {
  return (int16_t)readRegister(REG_SHUNT_V);
}

int16_t INA226::readBusVoltageRaw() {
  return (int16_t)readRegister(REG_BUS_V);
}

int16_t INA226::readCurrentRaw() {
  return (int16_t)readRegister(REG_CURRENT);
}

int16_t INA226::readPowerRaw() {
  return (int16_t)readRegister(REG_POWER);
}

float INA226::readShuntVoltage_mV() {
  return readShuntVoltageRaw() * 0.0025f;
}

float INA226::readBusVoltage_V() {
  return readBusVoltageRaw() * 0.00125f;
}

float INA226::readCurrent_mA() {
  return readCurrentRaw() * _current_LSB * 1000.0f;
}

float INA226::readPower_mW() {
  return readPowerRaw() * _current_LSB * 25.0f * 1000.0f;
}

INA226::Measurement INA226::readAll() {
  Measurement m{};

  if (!_wire) return m;

  _wire->beginTransmission(_addr);
  if (_wire->endTransmission() != 0) {
    // Only log first few failures per instance
    if (dbg_cnt < 20) {
      Serial.printf("[INA] 0x%02X: I2C NAK on readAll (offline?)\n", _addr);
      dbg_cnt++;
    }
    return m;
  }

  // Read shunt voltage and bus voltage directly
  int16_t shunt_raw = (int16_t)readRegister(REG_SHUNT_V);
  int16_t bus_raw   = (int16_t)readRegister(REG_BUS_V);

  float shunt_mV = shunt_raw * 0.0025f;    // 2.5¦ÌV per LSB
  float bus_V    = bus_raw * 0.00125f;      // 1.25mV per LSB

  m.shunt_voltage_mV = shunt_mV;
  m.bus_voltage_V    = bus_V;
  // Current = Vshunt / Rshunt (bypass calibration for transparency)
  // Sign convention: reversed so that CHARGING reads positive, supplying negative
  m.current_mA = -(shunt_mV / (_shunt_mohm / 1000.0f));
  m.power_mW   = bus_V * m.current_mA;

  return m;
}

uint16_t INA226::readManufacturerID() {
  return readRegister(REG_MFR_ID);
}

uint16_t INA226::readDieID() {
  return readRegister(REG_DIE_ID);
}

void INA226::writeRegister(uint8_t reg, uint16_t value) {
  if (!_wire) return;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write((value >> 8) & 0xFF);
  _wire->write(value & 0xFF);
  _wire->endTransmission();
}

uint16_t INA226::readRegister(uint8_t reg) {
  if (!_wire) return 0;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission() != 0) return 0;
  _wire->requestFrom(_addr, (uint8_t)2);
  if (_wire->available() < 2) return 0;
  uint16_t value = ((uint16_t)_wire->read() << 8) | _wire->read();
  return value;
}
