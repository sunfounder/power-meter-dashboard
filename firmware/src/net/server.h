#pragma once

#include <Arduino.h>

/**
 * Web Server for Power Meter
 *
 * Features:
 * - WiFi AP + STA dual mode with config page
 * - Real-time dashboard via WebSocket
 * - REST API for control and data
 * - CSV file download
 * - Settings persistence via NVS
 */

// NTP sync with China-friendly servers, using configured tz offset
void ntpSync();

class PowerMeterWebServer {
public:
  static PowerMeterWebServer &getInstance();

  bool begin();
  void update();

  // WiFi management
  void startAP();
  void startSTA(const char *ssid, const char *password);
  void stopWiFi();
  bool isConnected() const;

  // Broadcast latest measurement to WebSocket clients
  void broadcastData(const struct MeasurementSnapshot &snap);

  // Streaming history: push .dat samples as binary chunks (called from main loop)
  void streamTick();
  // Download: push raw file bytes with the same backpressure (called from main loop)
  void downloadTick();
  // Execute queued WS commands (stream/download/abort) — main loop only
  void processCmdQueue();

  // Notification
  void notifyAlarm(int channel, const char *message);

private:
  PowerMeterWebServer();

  void setupRoutes();
  void setupWebSocket();

  bool _running;
  bool _ap_mode;
  uint32_t _last_sta_retry;
  int _sta_retry_count;
  bool _sta_gave_up;
};

/* ---- REST API Endpoints ---- */
// GET  /api/status              — current measurement + per-channel recording states
// GET  /api/settings            — get all device settings
// POST /api/settings            — update device settings (JSON body)
// POST /api/channel/<ch>/record/start  — start recording on channel {"name":"test"}
// POST /api/channel/<ch>/record/stop   — stop recording on channel
// POST /api/channel/<ch>/name   — rename channel {"name":"NewName"}
// POST /api/buzzer/test         — test buzzer
// GET  /api/files               — list recorded CSV files
// GET  /api/download?file=xxx   — download CSV
// POST /api/delete?file=xxx     — delete a recording file
// POST /api/wifi                — configure WiFi STA
// WS   /ws                      — WebSocket for real-time data
