#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#define LOG_BUF_SIZE 80
#define LOG_LINE_MAX 120

class WebLog {
public:
  static WebLog &getInstance();
  void begin();
  void log(const char *fmt, ...);
  // Set true once LittleFS is mounted (from DataRecorder::begin)
  static bool fsReady;

  int count() const { return _count; }
  int total() const { return _total; }
  const char *loadAll();  // return entire log as string (caller must free)

private:
  WebLog();
  void _writeToFile(const char *line);
  char _buf[LOG_BUF_SIZE][LOG_LINE_MAX];
  int _head, _count, _total;
  static constexpr const char *LOG_PATH = "/data/system.log";
  static constexpr size_t MAX_FILE_SIZE = 32768;  // 32KB
};
