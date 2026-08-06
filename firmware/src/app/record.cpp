#include "record.h"
#include "config.h"
#include "../net/log.h"
#include "../hal/buzzer.h"
#include <time.h>
#include <ArduinoJson.h>

DataRecorder &DataRecorder::getInstance() {
  static DataRecorder instance;
  return instance;
}

// ── Beep flags (set in any task, consumed in loop) ──
static volatile bool s_start_beep_pending = false;
static volatile bool s_stop_beep_pending = false;

DataRecorder::DataRecorder() {
  memset(_ring_buf, 0, sizeof(_ring_buf));
  memset(_buf_head, 0, sizeof(_buf_head));
  memset(_buf_count, 0, sizeof(_buf_count));
  memset(_filenames, 0, sizeof(_filenames));
  memset(_states, 0, sizeof(_states));
  memset(_elapsed_buf, 0, sizeof(_elapsed_buf));
  s_start_beep_pending = false;
  s_stop_beep_pending = false;
}

bool DataRecorder::consumeStartBeep() {
  bool v = s_start_beep_pending;
  s_start_beep_pending = false;
  return v;
}
bool DataRecorder::consumeStopBeep() {
  bool v = s_stop_beep_pending;
  s_stop_beep_pending = false;
  return v;
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

void DataRecorder::_buildFilename(int ch, const char *testName, char *out, size_t out_sz) {
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
  snprintf(out, out_sz, "%s/%s_ch%d%s.dat", DATA_DIR, safe_name, ch + 1, ts);
}

bool DataRecorder::_openFile(int ch, const char *testName) {
  if (ch < 0 || ch > 3) return false;

  _buildFilename(ch, testName, _filenames[ch], sizeof(_filenames[ch]));
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

  // Enforce max recorded files: delete oldest .dat before starting a new one
  _enforceFileLimit();
  // Ensure free space for 8h at current sample rate (delete oldest if needed)
  _ensureSpaceFor8h();

  if (!_openFile(ch, testName)) return false;

  strncpy(_states[ch].name, testName, sizeof(_states[ch].name) - 1);
  _states[ch].name[sizeof(_states[ch].name) - 1] = '\0';
  _states[ch].start_time = millis();
  _states[ch].start_ts   = (uint32_t)time(nullptr);  // epoch sec (NTP); may be small if unsynced
  _states[ch].active = true;
  _states[ch].sample_count = 0;
  _states[ch].last_file[0] = '\0';
  _rename_pending[ch] = false;

  // Reset auto-stop armed state for a fresh session
  DeviceSettings::getInstance().setStopArmed(ch, false, false);

  _buf_head[ch] = 0;
  _buf_count[ch] = 0;
  s_start_beep_pending = true;
  return true;
}

// Resume an interrupted recording: reuse the existing file (append), strip any
// trailing magic, and continue with the real sample count.
bool DataRecorder::resumeChannel(int ch, const char *testName, const char *resume_file) {
  if (ch < 0 || ch > 3) return false;
  if (!resume_file || !resume_file[0]) return false;
  if (_states[ch].active) stopChannel(ch);

  String path = String("/data/") + resume_file;
  if (!LittleFS.exists(path)) return false;

  // (No magic to strip — interrupted files never got renamed to .done)
  size_t sz = LittleFS.open(path).size();

  strncpy(_filenames[ch], path.c_str(), sizeof(_filenames[ch]) - 1);
  _filenames[ch][sizeof(_filenames[ch]) - 1] = '\0';

  strncpy(_states[ch].name, testName, sizeof(_states[ch].name) - 1);
  _states[ch].name[sizeof(_states[ch].name) - 1] = '\0';
  _states[ch].active = true;
  _states[ch].sample_count = sz / sizeof(SampleBin);  // carry existing samples
  // Rewind start_time so the elapsed timer includes pre-crash history:
  // prefer real timestamps (last - first), fall back to samples × interval.
  size_t n = _states[ch].sample_count;
  if (n > 0) {
    uint32_t first_ts = 0, last_ts = 0;
    File rf = LittleFS.open(path);
    if (rf) {
      SampleBin b;
      if (rf.read((uint8_t *)&b, sizeof(SampleBin)) == sizeof(SampleBin)) first_ts = b.timestamp;
      rf.seek((n - 1) * sizeof(SampleBin));
      if (rf.read((uint8_t *)&b, sizeof(SampleBin)) == sizeof(SampleBin)) last_ts = b.timestamp;
      rf.close();
    }
    if (first_ts > 1600000000 && last_ts >= first_ts) {
      _states[ch].start_time = millis() - (uint32_t)(last_ts - first_ts) * 1000;
    } else {
      _states[ch].start_time = millis() - (uint32_t)n * DeviceSettings::getInstance().sampleIntervalMs();
    }
  } else {
    _states[ch].start_time = millis();
  }
  _states[ch].start_ts   = (uint32_t)time(nullptr);
  _states[ch].last_file[0] = '\0';
  _rename_pending[ch] = false;
  DeviceSettings::getInstance().setStopArmed(ch, false, false);
  _buf_head[ch] = 0;
  _buf_count[ch] = 0;
  s_start_beep_pending = true;
  Serial.printf("[DR] CH%d resumed %s (%lu existing samples)\n", ch + 1, resume_file, _states[ch].sample_count);
  return true;
}

// Find crash-interrupted recordings: no "PMDN" magic at EOF and recent by the
// timestamp embedded in the filename (test_chN_YYYYMMDD_HHMMSS.dat).
void DataRecorder::findIncomplete(char out[4][64]) {
  for (int i = 0; i < 4; i++) out[i][0] = '\0';
  File root = LittleFS.open("/data");
  if (!root || !root.isDirectory()) return;

  File f = root.openNextFile();
  while (f) {
    String nm = String(f.name());
    size_t sz = f.size();
    bool hasMagic = (sz % 24 == 4);
    f.close();

    if (nm.endsWith(".dat") && sz >= 24) {
      // Not renamed to .done = interrupted. Recent by filename timestamp.
      int p1 = nm.lastIndexOf('_');
      if (p1 >= 0) {
        int p2 = nm.lastIndexOf('.');
        String ts = nm.substring(p1 + 1, p2);  // YYYYMMDD_HHMMSS
        int y = ts.substring(0,4).toInt(), mo = ts.substring(4,6).toInt(), d = ts.substring(6,8).toInt();
        int h = ts.substring(9,11).toInt(), mi = ts.substring(11,13).toInt(), s = ts.substring(13,15).toInt();
        struct tm tm = {0};
        tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
        tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
        time_t ft = mktime(&tm);
        int8_t tz = DeviceSettings::getInstance().tzOffset();
        ft -= tz * 3600;  // filename time is local; epoch is UTC
        time_t now = time(nullptr);
        // Report any .dat file with a plausible filename timestamp (2020+).
        // NTP may not be synced right after a reset — don't gate on it.
        if (ft > 1600000000 && now > 1600000000 && (now - ft) < 48 * 3600) {  // within 48h
          const char *base = strrchr(nm.c_str(), '/');
          const char *bare = base ? base + 1 : nm.c_str();
          // Assign to a channel slot based on the chN marker in the name
          int chm = nm.indexOf("ch");
          int ch = (chm >= 0) ? (nm.substring(chm+2, chm+3).toInt() - 1) : 0;
          if (ch < 0 || ch > 3) ch = 0;
          if (out[ch][0] == '\0') strncpy(out[ch], bare, 63);
        }
      }
    }
    f = root.openNextFile();
  }
  root.close();
}

void DataRecorder::stopChannel(int ch) {
  if (ch < 0 || ch > 3) return;
  if (_states[ch].active) {
    // Rename pending? Move empty file to new name before flush
    if (_rename_pending[ch]) {
      char newname[64];
      _buildFilename(ch, _states[ch].name, newname, sizeof(newname));
      if (strcmp(newname, _filenames[ch]) != 0) {
        LittleFS.remove(newname);
        LittleFS.rename(_filenames[ch], newname);
        strncpy(_filenames[ch], newname, sizeof(_filenames[ch]) - 1);
      }
      _rename_pending[ch] = false;
    }
    _flushBuffer(ch);
    // Mark completion by renaming .dat → .done (crash recovery scans for .dat)
    {
      String p = String(_filenames[ch]);
      String done = p + ".done";
      LittleFS.remove(done);
      if (LittleFS.rename(p, done)) {
        strncpy(_filenames[ch], done.c_str(), sizeof(_filenames[ch]) - 1);
        _filenames[ch][sizeof(_filenames[ch]) - 1] = '\0';
      }
    }
    // Store bare filename (no /data/ prefix) for the web UI
    const char *base = strrchr(_filenames[ch], '/');
    strncpy(_states[ch].last_file, base ? base + 1 : _filenames[ch], sizeof(_states[ch].last_file) - 1);
    _states[ch].last_file[sizeof(_states[ch].last_file) - 1] = '\0';
    Serial.printf("[DR] CH%d stopped. %lu samples -> %s\n",
                  ch + 1, _states[ch].sample_count, _filenames[ch]);
    // Beep on stop — flag only; LEDC call happens in loop() context
    s_stop_beep_pending = true;
  }
  _states[ch].active = false;
}

void DataRecorder::renameCurrent(int ch, const char *newName) {
  if (ch < 0 || ch > 3) return;
  if (!_states[ch].active) return;
  strncpy(_states[ch].name, newName, sizeof(_states[ch].name) - 1);
  _states[ch].name[sizeof(_states[ch].name) - 1] = '\0';
  _rename_pending[ch] = true;
  Serial.printf("[DR] CH%d renamed to: %s\n", ch + 1, _states[ch].name);
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
  File root = LittleFS.open("/data");
  if (!root || !root.isDirectory()) { Serial.println("[DR] No data directory"); return; }
  Serial.println("[DR] Recorded files:");
  File f = root.openNextFile();
  while (f) {
    Serial.printf("  %s (%d bytes)\n", f.name(), f.size());
    f = root.openNextFile();
  }
  root.close();
}

// Fill a JsonArray with {name,size} for all .dat files (shared: HTTP + WS)
void DataRecorder::listFilesToJson(JsonArray &arr) {
  File root = LittleFS.open("/data");
  if (!root || !root.isDirectory()) return;
  File f = root.openNextFile();
  while (f) {
    String nm = String(f.name());
    size_t sz = f.size();
    f.close();
    // Keep only .dat files (skip .csv/others if any)
    if (nm.endsWith(".dat") || nm.endsWith(".done")) {
      // Strip "/data/" prefix
      const char *base = strrchr(nm.c_str(), '/');
      JsonObject o = arr.add<JsonObject>();
      o["name"] = base ? base + 1 : nm.c_str();
      o["size"] = sz;
    }
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

// Collect .dat filenames into names[] (heap-free, fixed size). Returns count.
static int collectDatFiles(char names[][64], int max) {
  File root = LittleFS.open("/data");
  if (!root || !root.isDirectory()) return 0;
  int n = 0;
  File f;
  while ((f = root.openNextFile()) && n < max) {
    String nm = String(f.name());
    f.close();
    if (nm.endsWith(".dat") || nm.endsWith(".done")) {
      strncpy(names[n], nm.c_str(), 63);
      names[n][63] = '\0';
      n++;
    }
  }
  root.close();
  return n;
}

// Delete the lexicographically smallest (= oldest by embedded timestamp) .dat.
// Returns true if one was removed.
static bool deleteOldestDat() {
  const int MAX = 32;
  char names[MAX][64];
  int n = collectDatFiles(names, MAX);
  if (n == 0) return false;
  int min_i = 0;
  for (int i = 1; i < n; i++) {
    if (strcmp(names[i], names[min_i]) < 0) min_i = i;
  }
  if (LittleFS.remove(names[min_i])) {
    Serial.printf("[DR] removed oldest %s\n", names[min_i]);
    return true;
  }
  return false;
}

// Keep at most MAX_REC_FILES .dat files: delete the oldest by name
// (names embed YYYYMMDD_HHMMSS timestamps, so lexical order ≈ time order).
void DataRecorder::_enforceFileLimit() {
  const int MAX = 32;
  char names[MAX][64];
  int n = collectDatFiles(names, MAX);
  while (n > MAX_REC_FILES) {
    int min_i = 0;
    for (int i = 1; i < n; i++) {
      if (strcmp(names[i], names[min_i]) < 0) min_i = i;
    }
    if (LittleFS.remove(names[min_i])) {
      Serial.printf("[DR] file limit: removed oldest %s\n", names[min_i]);
    }
    for (int i = min_i; i < n - 1; i++) strcpy(names[i], names[i + 1]);
    n--;
  }
}

// Ensure enough free space for 8 hours of recording at the current sample rate.
// Deletes oldest recordings until satisfied (or nothing left to delete).
void DataRecorder::_ensureSpaceFor8h() {
  uint32_t interval = DeviceSettings::getInstance().sampleIntervalMs();
  if (interval == 0) interval = 1000;
  size_t need = (8UL * 3600 * 1000 / interval) * sizeof(SampleBin);
  int guard = 0;
  while (LittleFS.usedBytes() + need > LittleFS.totalBytes() && guard < 32) {
    if (!deleteOldestDat()) break;
    guard++;
  }
  size_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (free < need) {
    Serial.printf("[DR] WARN: 8h @%lums needs %uKB but only %uKB free (max ~%.1fh)\n",
                  interval, (unsigned)(need / 1024), (unsigned)(free / 1024),
                  (double)free / need * 8.0);
  }
}
