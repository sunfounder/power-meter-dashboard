#pragma once

#include <Arduino.h>
#include <driver/gpio.h>

/**
 * Passive Buzzer Driver (non-blocking)
 *
 * Driven by N-MOSFET (AO3400A). Uses LEDC PWM for tone generation.
 * All patterns are non-blocking — call update() in main loop.
 *
 * CRITICAL: The pin used must NOT be a UART TX or other shared pin
 * that floats HIGH by default. On ESP32-S3, GPIO10 is the safest choice.
 */

class Buzzer {
public:
  Buzzer(uint8_t pin);

  void begin();

  // Single beep (non-blocking)
  void beep(unsigned int freq = 2400, unsigned long duration_ms = 200);

  // Multiple beeps (non-blocking)
  void beepN(int count, unsigned int freq = 2400,
             unsigned long on_ms = 100, unsigned long off_ms = 100);

  // Continuous tone (non-blocking)
  void startTone(unsigned int freq = 2400);
  void stopTone();

  // Repeating alarm (non-blocking)
  void startAlarm(unsigned int freq = 3000,
                  unsigned long on_ms = 200,
                  unsigned long off_ms = 200);
  void stopAlarm();

  // Must be called in main loop
  void update();

  // Pre-defined sound effects (non-blocking)
  void playTestComplete();
  void playWarning();

  bool isActive() const { return _active; }

private:
  uint8_t _pin;
  bool _active;

  enum Pattern { PAT_NONE, PAT_SINGLE, PAT_ALARM, PAT_SEQUENCE };
  Pattern _pattern;
  unsigned int _freq;
  unsigned long _on_ms;
  unsigned long _off_ms;
  unsigned long _pattern_start;
  int _beep_count;
  int _beep_index;
  bool _is_on;

  // For PAT_SEQUENCE (multi-tone non-blocking)
  int _sequence_step;
  unsigned long _sequence_wait_until;

  void _startPWM(unsigned int freq);
  void _stopPWM();
};
