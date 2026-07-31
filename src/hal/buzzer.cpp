#include "buzzer.h"

Buzzer::Buzzer(uint8_t pin)
    : _pin(pin), _active(false), _pattern(PAT_NONE),
      _freq(2400), _on_ms(200), _off_ms(100),
      _pattern_start(0), _beep_count(0), _beep_index(0), _is_on(false),
      _sequence_step(0), _sequence_wait_until(0) {}

void Buzzer::begin() {
  // Ensure pin is LOW before ANYTHING else
  // Always reconfigure as GPIO OUTPUT first to override serial/UART default
  gpio_reset_pin((gpio_num_t)_pin);
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
}

void Buzzer::_startPWM(unsigned int freq) {
  // gpio_reset_pin ensures no leftover UART/I2C/whatever config
  gpio_reset_pin((gpio_num_t)_pin);
  ledcSetup(1, freq, 8);
  ledcAttachPin(_pin, 1);
  ledcWrite(1, 128);  // 50% duty
}

void Buzzer::_stopPWM() {
  ledcDetachPin(_pin);
  // CRITICAL: after detach, the pin reverts to its pre-ledc state.
  // Explicitly reconfigure as GPIO OUTPUT LOW.
  gpio_reset_pin((gpio_num_t)_pin);
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
}

void Buzzer::beep(unsigned int freq, unsigned long duration_ms) {
  if (_active) _stopPWM();
  _pattern = PAT_SINGLE;
  _freq = freq;
  _on_ms = duration_ms;
  _beep_count = 1;
  _beep_index = 0;
  _pattern_start = millis();
  _is_on = true;
  _active = true;
  _startPWM(freq);
}

void Buzzer::beepN(int count, unsigned int freq,
                   unsigned long on_ms, unsigned long off_ms) {
  if (_active) _stopPWM();
  _pattern = PAT_ALARM;
  _freq = freq;
  _on_ms = on_ms;
  _off_ms = off_ms;
  _beep_count = count;
  _beep_index = 0;
  _pattern_start = millis();
  _is_on = true;
  _active = true;
  _startPWM(freq);
}

void Buzzer::startTone(unsigned int freq) {
  if (_active) _stopPWM();
  _active = true;
  _startPWM(freq);
}

void Buzzer::stopTone() {
  _stopPWM();
  _active = false;
  _pattern = PAT_NONE;
}

void Buzzer::startAlarm(unsigned int freq, unsigned long on_ms, unsigned long off_ms) {
  beepN(-1, freq, on_ms, off_ms);
}

void Buzzer::stopAlarm() {
  stopTone();
}

void Buzzer::update() {
  // Handle non-blocking sequences first
  if (_pattern == PAT_SEQUENCE) {
    unsigned long now = millis();
    if ((int32_t)(now - _sequence_wait_until) < 0) return; // still waiting
    _sequence_step++;
    switch (_sequence_step) {
      case 1:
        beep(2600, 100);
        _sequence_wait_until = millis() + 100 + 120;
        break;
      case 2:
        beep(3200, 200);
        _sequence_wait_until = millis() + 200 + 100;
        break;
      default:
        stopTone();
        break;
    }
    return;
  }

  if (!_active) return;
  unsigned long now = millis();

  switch (_pattern) {
    case PAT_SINGLE: {
      if ((int32_t)(now - _pattern_start) >= (int32_t)_on_ms) {
        stopTone();
      }
      break;
    }
    case PAT_ALARM: {
      if (_beep_count > 0 && _beep_index >= _beep_count) {
        stopTone();
        break;
      }

      if (_is_on && (int32_t)(now - _pattern_start) >= (int32_t)_on_ms) {
        _stopPWM();
        _is_on = false;
        _pattern_start = now;
        if (_beep_count > 0) _beep_index++;
      } else if (!_is_on && (int32_t)(now - _pattern_start) >= (int32_t)_off_ms) {
        _startPWM(_freq);
        _is_on = true;
        _pattern_start = now;
      }
      break;
    }
    default:
      break;
  }
}

void Buzzer::playTestComplete() {
  // Non-blocking ascending 3-tone sequence via PAT_SEQUENCE
  if (_active) stopTone();
  _pattern = PAT_SEQUENCE;
  _active = true;
  _sequence_step = 0;
  beep(2000, 100);
  _sequence_wait_until = millis() + 100 + 120; // beep duration + gap
}

void Buzzer::playWarning() {
  beepN(3, 3000, 150, 100);
}
