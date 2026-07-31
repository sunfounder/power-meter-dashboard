#include "log.h"
#include <stdarg.h>

WebLog &WebLog::getInstance() {
  static WebLog instance;
  return instance;
}

WebLog::WebLog() : _head(0), _count(0), _total(0) {
  memset(_buf, 0, sizeof(_buf));
}

void WebLog::begin() {
  log("WebLog started");
}

void WebLog::log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(_buf[_head], LOG_LINE_MAX, fmt, args);
  va_end(args);

  _head = (_head + 1) % LOG_BUF_SIZE;
  if (_count < LOG_BUF_SIZE) _count++;
  _total++;
}

const char *WebLog::getLine(int idx) const {
  if (idx < 0 || idx >= _count) return nullptr;
  int pos = (_head - _count + idx) % LOG_BUF_SIZE;
  if (pos < 0) pos += LOG_BUF_SIZE;
  return _buf[pos];
}
