#include "record.h"
#include "config.h"
#include "../net/log.h"
#include <time.h>

DataRecorder &DataRecorder::getInstance() {
  static DataRecorder instance;
  return instance;
}

DataRecorder::DataRecorder() {
  memset(_ring_buf, 0, sizeof(_ring_buf));
  memset(_buf_head, 0, sizeof(_buf_head));
  memset(_buf_count, 0, sizeof(_buf_count));
  memset(_filenames, 0, sizeof(_filenames));
  memset(_states, 0, sizeof(_states));
  memset(_elapsed_buf, 0, sizeof(_elapsed_buf));
}

bool DataRecorder::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[DR] LittleFS mount failed!");
    return false;
  }
  WebLog::fsReady = true;
  Serial.printf("[DR] LittleFS mounted, total=%dKB, used=%dKB\n",
                LittleFS.totalBytes() / 1024,
                LittleFS.usedBytes() / 1024);
  if (!LittleFS.exists(DATA_DIR)) {
    LittleFS.mkdir(DATA_DIR);
  }
  return true;
}

bool DataRecorder::_openFile(int ch, const char *testName) {
  if (ch < 0 || ch > 3) return false;

  char safe_name[32];
  int j = 0;
  for (int i = 0; testName[i] && j < 28; i++) {
    char c = testName[i];
    if (isalnum(c) || c == '-' || c == '_') safe_name[j++] = c;
    else if (c == ' ') safe_name[j++] = '_';
  }
  safe_name[j] = '\0';
  if (j == 0) strcpy(safe_name, "record");

  char ts[20];
  time_t now = time(nullptr);
  int8_t tz = DeviceSettings::getInstance().tzOffset();
  now += tz * 3600;
  if (now > 1600000000) {
    struct tm *tm = localtime(&now);
    snprintf(ts, sizeof(ts), "_%04d%02d%02d_%02d%02d%02d",
             tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
  } else {
    snprintf(ts, sizeof(ts), "_%08lx", millis() & 0xFFFFFFFF);
  }
  snprintf(_filenames[ch], sizeof(_filenames[ch]),
           "%s/%s_ch%d%s.dat", DATA_DIR, safe_name, ch + 1, ts);

  // Create empty .dat file (no header needed)
  if (!LittleFS.exists(_filenames[ch])) {
    File f = LittleFS.open(_filenames[ch], FILE_WRITE);
    if (!f) { Serial.printf("[DR] Failed to create: %s\n", _filenames[ch]); return false; }
    f.close();
    delay(20);
    yield();
  }

  Serial.printf("[DR] CH%d ready: %s\n", ch + 1, _filenames[ch]);
  return true;
}

void DataRecorder::_flushBuffer(int ch) {
  if (_buf_count[ch] == 0) return;
  if (_filenames[ch][0] == '\0') return;

  Serial.printf("[DR] Flushing CH%d: %d samples to %s\n",
                ch + 1, _buf_count[ch], _filenames[ch]);

  File f = LittleFS.open(_filenames[ch], FILE_APPEND);
  if (!f) {
    Serial.printf("[DR] Flush failed: cannot open %s\n", _filenames[ch]);
    _buf_count[ch] = 0;
    return;
  }

  int start = (_buf_head[ch] - _buf_count[ch] + BUF_ROWS) % BUF_ROWS;
  for (uint16_t i = 0; i < _buf_count[ch]; i++) {
    int idx = (start + i) % BUF_ROWS;
    f.write((const uint8_t *)&_ring_buf[ch][idx], sizeof(SampleBin));
  }

  f.flush();
  f.close();
  delay(10);
  yield();

  _buf_count[ch] = 0;
  Serial.printf("[DR] CH%d flush done, total: %lu\n", ch + 1, _states[ch].sample_count);
}

bool DataRecorder::startChannel(int ch, const char *testName) {
  if (ch < 0 || ch > 3) return false;
  if (_states[ch].active) stopChannel(ch);

  if (!_openFile(ch, testName)) return false;

  strncpy(_states[ch].name, testName, sizeof(_states[ch].name) - 1);
  _states[ch].name[sizeof(_states[ch].name) - 1] = '\0';
  _states[ch].start_time = millis();
  _states[ch].active = true;
  _states[ch].sample_count = 0;
  _states[ch].last_file[0] = '\0';

  _buf_head[ch] = 0;
  _buf_count[ch] = 0;
  return true;
}

void DataRecorder::stopChannel(int ch) {
  if (ch < 0 || ch > 3) return;
  if (_states[ch].active) {
    _flushBuffer(ch);
    strncpy(_states[ch].last_file, _filenames[ch], sizeof(_states[ch].last_file) - 1);
    _states[ch].last_file[sizeof(_states[ch].last_file) - 1] = '\0';
    Serial.printf("[DR] CH%d stopped. %lu samples -> %s\n",
                  ch + 1, _states[ch].sample_count, _filenames[ch]);
  }
  _states[ch].active = false;
}

bool DataRecorder::isChannelRecording(int ch) const {
  if (ch < 0 || ch > 3) return false;
  return _states[ch].active;
}

bool DataRecorder::isAnyRecording() const {
  for (int i = 0; i < 4; i++) if (_states[i].active) return true;
  return false;
}

bool DataRecorder::appendSample(const MeasurementSnapshot &snap) {
  bool wrote = false;
  for (int ch = 0; ch < 4; ch++) {
    if (!_states[ch].active) continue;

    auto &s = snap.channels[ch];
    SampleBin &bin = _ring_buf[ch][_buf_head[ch]];
    bin.timestamp       = snap.timestamp_ms;
    bin.bus_voltage_V   = s.bus_voltage_V;
    bin.current_A       = s.current_mA / 1000.0f;
    bin.power_W         = s.power_mW / 1000.0f;
    bin.channel_temp_C  = s.channel_temp_C;
    bin.ambient_temp_C  = snap.env.ambient_temp_C;

    _buf_head[ch] = (_buf_head[ch] + 1) % BUF_ROWS;
    if (_buf_count[ch] < BUF_ROWS) _buf_count[ch]++;

    _states[ch].sample_count++;
    wrote = true;

    if (_buf_count[ch] >= BUF_ROWS) _flushBuffer(ch);
  }
  return wrote;
}

void DataRecorder::stopAll() {
  for (int ch = 0; ch < 4; ch++) stopChannel(ch);
}

const char *DataRecorder::elapsedStr(int ch) {
  if (ch < 0 || ch > 3 || !_states[ch].active) return "00:00";
  uint32_t elapsed = millis() - _states[ch].start_time;
  uint32_t mins = (elapsed / 60000) % 100;
  uint32_t secs = (elapsed % 60000) / 1000;
  snprintf(_elapsed_buf, sizeof(_elapsed_buf), "%02lu:%02lu", mins, secs);
  return _elapsed_buf;
}

void DataRecorder::listFiles() {
  File root = LittleFS.open(DATA_DIR);
  if (!root || !root.isDirectory()) { Serial.println("[DR] No data directory"); return; }
  Serial.println("[DR] Recorded files:");
  File f = root.openNextFile();
  while (f) {
    Serial.printf("  %s (%d bytes)\n", f.name(), f.size());
    f = root.openNextFile();
  }
  root.close();
}

size_t DataRecorder::getFileSize(const char *path) {
  if (!LittleFS.exists(path)) return 0;
  File f = LittleFS.open(path);
  if (!f) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

bool DataRecorder::deleteFile(const char *path) {
  if (!LittleFS.exists(path)) return false;
  return LittleFS.remove(path);
}
