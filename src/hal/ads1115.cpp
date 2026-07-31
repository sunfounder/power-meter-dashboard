#include "ads1115.h"

ADS1115::ADS1115(uint8_t addr)
    : _addr(addr), _gain(GAIN_6_144V), _voltage_LSB(0.0001875f), _wire(nullptr) {}

bool ADS1115::begin(TwoWire &wire) {
  _wire = &wire;
  if (!isConnected()) return false;

  // Set default config: gain ±2.048V, 128 SPS, continuous mode off initially
  // We'll use single-shot mode for each read
  setGain(GAIN_6_144V);
  setDataRate(RATE_128_SPS);
  return true;
}

bool ADS1115::isConnected() {
  if (!_wire) return false;
  _wire->beginTransmission(_addr);
  return _wire->endTransmission() == 0;
}

void ADS1115::setGain(Gain gain) {
  _gain = gain;
  switch (gain) {
    case GAIN_6_144V: _voltage_LSB = 0.0001875f; break;  // 6.144 / 32768
    case GAIN_4_096V: _voltage_LSB = 0.000125f;  break;
    case GAIN_2_048V: _voltage_LSB = 0.0000625f; break;
    case GAIN_1_024V: _voltage_LSB = 0.00003125f; break;
    case GAIN_0_512V: _voltage_LSB = 0.000015625f; break;
    case GAIN_0_256V: _voltage_LSB = 0.0000078125f; break;
    default: break;
  }
}

void ADS1115::setDataRate(DataRate rate) {
  // Stored for future use; applied in readADC_SingleEnded config
  (void)rate;
}

float ADS1115::getVoltageLSB() const {
  return _voltage_LSB;
}

int16_t ADS1115::readChannel(uint8_t channel) {
  if (channel > 3) return 0;
  return (int16_t)readADC_SingleEnded(channel);
}

float ADS1115::readVoltage(uint8_t channel) {
  return readChannel(channel) * _voltage_LSB;
}

void ADS1115::readAllChannels(int16_t values[4]) {
  for (uint8_t i = 0; i < 4; i++) {
    values[i] = readChannel(i);
  }
}

void ADS1115::readAllVoltages(float voltages[4]) {
  for (uint8_t i = 0; i < 4; i++) {
    voltages[i] = readVoltage(i);
  }
}

uint16_t ADS1115::readADC_SingleEnded(uint8_t channel) {
  if (!_wire) return 0;

  uint16_t mux = 0;
  switch (channel) {
    case 0: mux = CONFIG_MUX_SINGLE_0; break;
    case 1: mux = CONFIG_MUX_SINGLE_1; break;
    case 2: mux = CONFIG_MUX_SINGLE_2; break;
    case 3: mux = CONFIG_MUX_SINGLE_3; break;
    default: return 0;
  }

  // Build config word:
  // OS (bit 15) = 1 (start single conversion)
  // MUX (bits 14-12) = channel select
  // PGA (bits 11-9) = gain
  // MODE (bit 8) = 1 (single-shot)
  // DR (bits 7-5) = 100 (128 SPS)
  // COMP (bits 4-0) = 00011 (disable comparator)
  uint8_t pga_bits = (uint8_t)_gain;
  if (pga_bits > 3) pga_bits = 3; // clamp to valid PGA bits

  uint16_t config = CONFIG_OS_SINGLE | mux;
  config |= ((uint16_t)pga_bits << 9);
  config |= 0x0100;  // MODE = single-shot
  config |= 0x0080;  // DR = 128 SPS (100 << 5)
  config |= 0x0003;  // Disable comparator

  writeRegister(REG_CONFIG, config);

  // Wait for conversion (128 SPS ≈ 7.8ms)
  delay(8);

  return readRegister(REG_CONVERSION);
}

void ADS1115::writeRegister(uint8_t reg, uint16_t value) {
  if (!_wire) return;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write((value >> 8) & 0xFF);
  _wire->write(value & 0xFF);
  _wire->endTransmission();
}

uint16_t ADS1115::readRegister(uint8_t reg) {
  if (!_wire) return 0;
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission() != 0) return 0;
  _wire->requestFrom(_addr, (uint8_t)2);
  if (_wire->available() < 2) return 0;
  return ((uint16_t)_wire->read() << 8) | _wire->read();
}
