#pragma once

#include "measure_types.h"
#include <FS.h>
#include <LittleFS.h>

/**
 * Data Recorder — binary storage (.dat files)
 *
 * Each sample is stored as a fixed-size binary struct (24 bytes).
 * Ring buffer in RAM → flushed to LittleFS on stop.
 *
 * File format: .dat, no header, just packed SampleBin structs.
 */

#pragma pack(push, 1)
struct SampleBin {
  uint32_t timestamp;     // UNIX seconds
  float bus_voltage_V;
  float current_A;
  float power_W;
  float channel_temp_C;
  float ambient_temp_C;
};
#pragma pack(pop)

class DataRecorder {
public:
  static DataRecorder &getInstance();

  bool begin();

  bool startChannel(int ch, const char *testName);
  void stopChannel(int ch);
  bool isChannelRecording(int ch) const;
  bool isAnyRecording() const;

  bool appendSample(const MeasurementSnapshot &snap);
  void stopAll();

  const ChannelRecordingState &channelState(int ch) const { return _states[ch]; }
  const char *elapsedStr(int ch);
  const char *currentFilename(int ch) const { return _filenames[ch]; }

  // Get raw buffer for API (returns pointer + count)
  const SampleBin *bufferData(int ch) const { return _ring_buf[ch]; }
  uint16_t bufferCount(int ch) const { return _buf_count[ch]; }
  uint16_t bufferHead(int ch) const { return _buf_head[ch]; }

  void listFiles();
  size_t getFileSize(const char *path);
  bool deleteFile(const char *path);

private:
  DataRecorder();

  static constexpr size_t BUF_ROWS = 60;  // 60 samples per channel (~24KB total)
  SampleBin _ring_buf[4][BUF_ROWS];
  uint16_t _buf_head[4];
  uint16_t _buf_count[4];

  char _filenames[4][64];
  ChannelRecordingState _states[4];
  char _elapsed_buf[6];

  bool _openFile(int ch, const char *testName);
  void _flushBuffer(int ch);

  static constexpr const char *DATA_DIR = "/data";
};
