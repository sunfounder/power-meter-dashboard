#pragma once

#include <Arduino.h>

/**
 * Single Channel Control
 *
 * V1 simplified: only EN0 (master switch) per channel.
 * EN1 is hardwired ON, EN2 is not used.
 *
 * V2 will restore EN1/EN2 selection via 2-IO scheme (EN + SEL).
 */

class ChannelControl {
public:
  ChannelControl(uint8_t en0_pin, const char *name = "CH");

  void begin();

  // Master switch
  void enable(bool on);
  bool isEnabled() const { return _state; }

  // Turn off
  void off();

  // Channel name
  const char *name() const { return _name; }

private:
  uint8_t _pin;
  const char *_name;
  bool _state;
};

/**
 * 4-Channel Control Manager
 *
 * Manages all 4 channels' EN0 signals.
 * When recording starts/stops, all channels' EN0 follows.
 */
class ChannelManager {
public:
  ChannelManager();
  void begin();

  ChannelControl &channel(int idx);  // 0-3

  void startRecording();
  void stopRecording();
  bool isRecording() const { return _recording; }

private:
  ChannelControl _channels[4];
  bool _recording;
};
