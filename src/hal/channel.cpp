#include "channel.h"
#include "pin_config.h"

ChannelControl::ChannelControl(uint8_t en0_pin, const char *name)
    : _pin(en0_pin), _name(name), _state(false) {}

void ChannelControl::begin() {
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  _state = false;
}

void ChannelControl::enable(bool on) {
  _state = on;
  digitalWrite(_pin, on ? HIGH : LOW);
}

void ChannelControl::off() {
  enable(false);
}

// ---- ChannelManager ----

ChannelManager::ChannelManager()
    : _channels{
        ChannelControl(PIN_CHA_EN, "CH1"),
        ChannelControl(PIN_CHB_EN, "CH2"),
        ChannelControl(PIN_CHC_EN, "CH3"),
        ChannelControl(PIN_CHD_EN, "CH4")
      }, _recording(false) {}

void ChannelManager::begin() {
  for (int i = 0; i < 4; i++) {
    _channels[i].begin();
    _channels[i].enable(true);  // Power on all channels at boot
  }
}

ChannelControl &ChannelManager::channel(int idx) {
  return _channels[idx];
}

void ChannelManager::startRecording() {
  _recording = true;
  for (int i = 0; i < 4; i++) {
    _channels[i].enable(true);
  }
}

void ChannelManager::stopRecording() {
  _recording = false;
  for (int i = 0; i < 4; i++) {
    _channels[i].enable(false);
  }
}
