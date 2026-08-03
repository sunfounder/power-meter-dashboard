#include "log.h"
#include <cstdarg>
#include <cstdio>

WebLog &WebLog::getInstance() { static WebLog i; return i; }
WebLog::WebLog() : _head(0), _count(0), _total(0) { memset(_buf, 0, sizeof(_buf)); }

void WebLog::begin() {}

void WebLog::log(const char *fmt, ...) {
  char line[LOG_LINE_MAX];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  // Ring buffer
  strncpy(_buf[_head], line, LOG_LINE_MAX - 1);
  _head = (_head + 1) % LOG_BUF_SIZE;
  if (_count < LOG_BUF_SIZE) _count++;
  _total++;

  // Persist to LittleFS
  _writeToFile(line);
  Serial.println(line);
}

void WebLog::_writeToFile(const char *line) {
  File f = LittleFS.open(LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.println(line);
  size_t sz = f.size();
  f.close();
  // Truncate if too large: keep last MAX_FILE_SIZE bytes
  if (sz > MAX_FILE_SIZE) {
    f = LittleFS.open(LOG_PATH, FILE_READ);
    if (!f) return;
    f.seek(sz - MAX_FILE_SIZE);
    String tail = f.readString();
    f.close();
    f = LittleFS.open(LOG_PATH, FILE_WRITE);
    if (f) { f.print(tail); f.close(); }
  }
}

const char *WebLog::loadAll() {
  if (!LittleFS.exists(LOG_PATH)) return strdup("");
  File f = LittleFS.open(LOG_PATH);
  if (!f) return strdup("");
  String s = f.readString();
  f.close();
  return strdup(s.c_str());
}
