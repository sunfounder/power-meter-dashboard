#pragma once
#include <Arduino.h>

// Ring buffer log — stores last N lines for web display
#define LOG_BUF_SIZE 50
#define LOG_LINE_MAX 120

class WebLog {
public:
  static WebLog &getInstance();

  void begin();
  void log(const char *fmt, ...);
  const char *getLine(int idx) const;  // 0 = oldest, -1 unused
  int count() const { return _count; }
  int total() const { return _total; }

private:
  WebLog();
  char _buf[LOG_BUF_SIZE][LOG_LINE_MAX];
  int _head;   // next write position
  int _count;  // number of lines stored (max BUF_SIZE)
  int _total;  // total lines ever written
};
