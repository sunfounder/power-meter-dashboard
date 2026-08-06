#include "server.h"


#include "log.h"
#include "../app/measure_types.h"
#include "../app/measure.h"
#include "../app/record.h"
#include "../app/config.h"
#include "../version.h"
#include "../hal/channel.h"
#include "../hal/buzzer.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <lvgl.h>

extern lv_display_t *g_lv_disp; // from main.cpp, for live rotation

static AsyncWebServer _server(80);

// Guard against client storms: cap concurrent connections (prevents
// AsyncTCP task saturation from many polling clients)
static const int MAX_HTTP_CLIENTS = 4;
static AsyncWebSocket _ws("/ws");

static bool _running = false;
static bool _ap_mode = true;

// Forward declarations
static void handleStatus(AsyncWebServerRequest *request);
static void handleSettings(AsyncWebServerRequest *request);
static void handleChannelRecordStart(AsyncWebServerRequest *request);
static void handleChannelRecordStop(AsyncWebServerRequest *request);
static void handleChannelRename(AsyncWebServerRequest *request);
static void handleBuzzerTest(AsyncWebServerRequest *request);
static void handleListFiles(AsyncWebServerRequest *request);
static void handleDownload(AsyncWebServerRequest *request);
static void handleDeleteFile(AsyncWebServerRequest *request);
static void handleWifiConfig(AsyncWebServerRequest *request);
static void handleChannelRecordAll(AsyncWebServerRequest *request);
// Shared JSON builders (WS + HTTP)
static void settingsToJson(JsonDocument &doc);
static void channelRecordAllToJson(JsonArray &arr, int ch, int offset, int limit);

// GET /api/storage — LittleFS space info
static void handleStorage(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["total_kb"]  = LittleFS.totalBytes() / 1024;
  doc["used_kb"]   = LittleFS.usedBytes() / 1024;
  doc["free_kb"]   = (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024;
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len);

// Helper: parse channel index from URL pattern "/api/channel/<ch>/..."
static int parseChannelFromUrl(const String &url) {
  int ch_start = url.indexOf("channel/") + 8;
  if (ch_start <= 7) return -1;
  int ch_end = url.indexOf("/", ch_start);
  if (ch_end < 0) ch_end = url.length();
  return url.substring(ch_start, ch_end).toInt();
}

// ---- PowerMeterWebServer ----

PowerMeterWebServer &PowerMeterWebServer::getInstance() {
  static PowerMeterWebServer instance;
  return instance;
}

PowerMeterWebServer::PowerMeterWebServer() : _running(false), _ap_mode(true), _last_sta_retry(0), _sta_retry_count(0), _sta_gave_up(false) {}

bool PowerMeterWebServer::begin() {
  setupRoutes();
  setupWebSocket();
  _server.begin();
  _running = true;
  Serial.println("[WS] Web server started on port 80");
  Serial.println("[WS] API-only mode (dashboard on PC)");
  return true;
}

void PowerMeterWebServer::update() {
  _ws.cleanupClients();

  // Two-stage STA retry
  auto &cfg = DeviceSettings::getInstance();
  if (strlen(cfg.wifiSSID()) > 0 && WiFi.status() != WL_CONNECTED && !_sta_gave_up) {
    uint32_t now = millis();
    bool stage1 = (_sta_retry_count < 5);  // first 5 attempts: 5s interval
    uint32_t interval = stage1 ? 5000 : 30000;
    int max_attempts = 10;

    if (now - _last_sta_retry > interval) {
      _last_sta_retry = now;
      _sta_retry_count++;
      Serial.printf("[WS] STA retry %d/%d: %s\n", _sta_retry_count, max_attempts, cfg.wifiSSID());
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(cfg.wifiSSID(), cfg.wifiPassword());

      if (_sta_retry_count >= max_attempts) {
        _sta_gave_up = true;
        Serial.println("[WS] STA: gave up after 10 attempts");
      }
    }
  }

  // Reset retry if connected
  if (WiFi.status() == WL_CONNECTED) {
    if (_sta_retry_count > 0) { // Just connected
      int8_t tz = DeviceSettings::getInstance().tzOffset();
      ntpSync();
      Serial.printf("[WS] NTP sync started (UTC%+d)\n", tz);
      Serial.printf("[WS] STA connected! IP: %s\n", WiFi.localIP().toString().c_str());
    }
    _sta_retry_count = 0;
    _sta_gave_up = false;
  }
}

// NTP sync with China-friendly servers, using configured tz offset
void ntpSync() {
  int8_t tz = DeviceSettings::getInstance().tzOffset();
  configTime(tz * 3600, 0, "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org");
}

void PowerMeterWebServer::startAP() {
  _ap_mode = true;
  auto &cfg = DeviceSettings::getInstance();

  // AP SSID = device_name + "-" + last 3 bytes of MAC
  // Use base MAC (WiFi.macAddress) — stable from eFuse, unlike
  // softAPmacAddress which returns garbage before WiFi init.
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ap_ssid[40];
  snprintf(ap_ssid, sizeof(ap_ssid), "%s-%02X%02X%02X",
           cfg.deviceName(), mac[3], mac[4], mac[5]);

  const char *pass = cfg.apPassword();

  // Validate password: ESP32 softAP requires >=8 chars or empty (open)
  int pass_len = strlen(pass);
  if (pass_len > 0 && pass_len < 8) {
    Serial.printf("[WS] AP password too short (%d chars), using defaults\n", pass_len);
    strcpy(ap_ssid, "PowerMeter-4CH");
    pass = "12345678";
  }

  Serial.printf("[WS] Starting AP: SSID=\"%s\" PASS=\"%s\" (len=%d)\n",
                ap_ssid, pass, strlen(pass));

  // Force clean WiFi state
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);

  bool ok = WiFi.softAP(ap_ssid, pass);
  Serial.printf("[WS] WiFi.softAP returned: %s\n", ok ? "true" : "false");

  if (ok) {
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WS] AP active: %s @ %s\n", ap_ssid, ip.toString().c_str());
    WebLog::getInstance().log("AP: %s @ %s", ap_ssid, ip.toString().c_str());

    // Auto-connect STA if credentials saved
    const char *sta_ssid = cfg.wifiSSID();
    if (strlen(sta_ssid) > 0) {
      Serial.printf("[WS] Auto-connecting STA: %s\n", sta_ssid);
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(sta_ssid, cfg.wifiPassword());
      // Don't block �?connection will complete in background
    }
  } else {
    Serial.println("[WS] AP start FAILED, retrying with hardcoded defaults...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);
    ok = WiFi.softAP("PowerMeter-4CH", "12345678");
    if (ok) {
      Serial.printf("[WS] AP fallback OK: PowerMeter-4CH @ %s\n",
                    WiFi.softAPIP().toString().c_str());
    } else {
      Serial.println("[WS] FATAL: AP failed even with defaults!");
    }
  }
}

void PowerMeterWebServer::startSTA(const char *ssid, const char *password) {
  _ap_mode = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("[WS] Connecting to %s...\n", ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Sync NTP time (use configured tz offset)
    ntpSync();
    Serial.println("[WS] NTP time sync started");
    WebLog::getInstance().log("STA connected: %s IP=%s", ssid, WiFi.localIP().toString().c_str());

    Serial.printf("[WS] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    // Save WiFi credentials
    DeviceSettings::getInstance().setWiFi(ssid, password);
    DeviceSettings::getInstance().save();
  } else {
    Serial.println("[WS] WiFi connect failed, falling back to AP");
    startAP();
  }
}

void PowerMeterWebServer::stopWiFi() {
  _ws.closeAll();
  WiFi.disconnect(true);
  _running = false;
}

bool PowerMeterWebServer::isConnected() const {
  return _ap_mode ? true : (WiFi.status() == WL_CONNECTED);
}

// Shared: serialize one measurement channel into a JSON object
static void channelToJson(JsonObject ch, const ChannelSample &c) {
  ch["bus_voltage_V"]    = c.bus_voltage_V;
  ch["shunt_voltage_mV"] = c.shunt_voltage_mV;
  ch["current_mA"]       = c.current_mA;
  ch["power_mW"]         = c.power_mW;
  ch["channel_temp_C"]   = c.channel_temp_C;
  ch["connected"]        = c.connected;
}

// Set when a WS command needs the send queue (e.g. large record_all responses):
// broadcast pauses for a while so the response isn't stuck behind 1s broadcasts.
volatile uint32_t ws_send_busy_until = 0;

// ── History streaming state machine ──
// stream_start → main loop pushes binary chunks (SampleBin[]), then stream_done.
static int  s_stream_ch = -1;        // active channel, -1 = idle
static int  s_stream_client = -1;    // requesting client id
static File s_stream_file;           // open .dat (invalid when file part done)
static uint32_t s_stream_off = 0;    // next sample index to send
static uint32_t s_stream_n_file = 0; // samples in file part
static uint32_t s_stream_total = 0;  // total samples (file + ring buffer)
static uint32_t s_stream_last = 0;   // last tick ms
static int  s_stream_fail = 0;       // consecutive send failures (abort guard)
static int  s_done_fail = 0;         // consecutive done-send failures (abort guard)

// Abort a stream cleanly from ANY path: close the file handle, reset state,
// resume broadcasts. This is the only place that tears the stream down, so no
// path can leak the file handle (LittleFS runs out of handles after 1-2 leaks).
static void streamAbort(const char *why) {
  if (s_stream_file) s_stream_file.close();
  s_stream_ch = -1;
  s_stream_client = -1;
  s_stream_fail = 0;
  s_done_fail = 0;
  ws_send_busy_until = 0;
  Serial.printf("[STREAM] aborted: %s\n", why);
}

void PowerMeterWebServer::streamTick() {
  if (s_stream_ch < 0) return;
  uint32_t now = millis();
  if (now - s_stream_last < 10) return;  // throttle ~100 chunks/s (windowed TX, backpressure below)
  s_stream_last = now;

  // Client may have disconnected mid-stream (e.g. page refresh) — abort cleanly
  auto *cl = _ws.client(s_stream_client);
  if (!cl || cl->status() != WS_CONNECTED) {
    streamAbort("client gone");
    return;
  }

  auto &rec = DataRecorder::getInstance();

  SampleBin chunk[100];  // 100 × 24B = 2400B per frame — big enough to trigger
                        // lwIP windowed TX (multiple TCP segments in flight) instead
                        // of the slow one-message-per-ACK path.
  size_t n = 0;

  // 1. File portion
  if (s_stream_off < s_stream_n_file) {
    size_t want = min((size_t)30, (size_t)(s_stream_n_file - s_stream_off));
    size_t got = s_stream_file.read((uint8_t *)chunk, want * sizeof(SampleBin));
    n = got / sizeof(SampleBin);
    s_stream_off += n;
    if (s_stream_off >= s_stream_n_file) s_stream_file.close();
  } else {
    // 2. Ring buffer portion (recording continues — samples after file part)
    uint16_t cnt = rec.bufferCount(s_stream_ch);
    uint32_t bufStart = s_stream_off - s_stream_n_file;
    if (bufStart < cnt) {
      uint16_t head = rec.bufferHead(s_stream_ch);
      size_t want = min((size_t)30, (size_t)(cnt - bufStart));
      for (size_t k = 0; k < want; k++) {
        int idx = (head - cnt + bufStart + k + 60) % 60;
        chunk[n++] = rec.bufferData(s_stream_ch)[idx];
      }
      s_stream_off += want;
    }
  }

  if (n > 0) {
    // Backpressure: if the WS send queue hasn't drained, pause until it does.
    // This makes the stream self-throttle to whatever the link can actually
    // deliver (no more queue overflow → connection drops).
    if (!cl->canSend()) {
      s_stream_off -= n;  // rewind: retry this chunk next tick
      s_stream_fail = 0;  // not a failure — just waiting
      return;
    }
    if (cl->binary((uint8_t *)chunk, n * sizeof(SampleBin))) {
      s_stream_fail = 0;  // sent OK
    } else {
      // Send failed (queue full / dead connection): a few retries, then abort
      // so we never spin forever or leak the file handle.
      s_stream_off -= n;
      if (++s_stream_fail > 5) streamAbort("send failed 5x");
    }
    return;
  }

  // Done
  JsonDocument done;
  done["type"] = "stream_done";
  done["ch"] = s_stream_ch;
  done["total"] = s_stream_total;
  String j;
  serializeJson(done, j);
  // Done (retry a few times if the send queue is full, then abort)
  if (!cl->text(j)) {
    if (++s_done_fail > 10) streamAbort("done send failed 10x");
    else Serial.println("[STREAM] done send failed, retrying");
    return;  // keep s_stream_ch, retry next tick
  }
  s_done_fail = 0;
  ws_send_busy_until = 0;  // resume broadcasts
  s_stream_ch = -1;
  s_stream_client = -1;
  Serial.printf("[STREAM] ch%d done, %u samples\n", s_stream_ch, s_stream_total);
}

void PowerMeterWebServer::broadcastData(const MeasurementSnapshot &snap) {
  if (!_running) return;
  if (millis() < ws_send_busy_until) return;  // paused: large response in flight

  auto &cfg = DeviceSettings::getInstance();
  auto &recorder = DataRecorder::getInstance();

  JsonDocument doc;
  doc["type"] = "measurement";
  JsonObject data = doc["data"].to<JsonObject>();

  data["timestamp"] = snap.timestamp_ms;
  JsonArray ch_arr = data["channels"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject ch = ch_arr.add<JsonObject>();
    channelToJson(ch, snap.channels[i]);
  }
  data["env"]["ambient_temp_C"] = snap.env.ambient_temp_C;

  // Per-channel recording states
  JsonArray rec_arr = data["recording"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject r = rec_arr.add<JsonObject>();
    r["active"]  = recorder.isChannelRecording(i);
    r["elapsed"] = recorder.elapsedStr(i);
    r["samples"] = recorder.channelState(i).sample_count;
    r["last_file"] = recorder.channelState(i).last_file;
    r["name"]    = recorder.channelState(i).name;
  }

  // Aggregate
  data["any_recording"] = recorder.isAnyRecording();
  data["temp_unit"] = String(DeviceSettings::getInstance().tempUnit());
  data["scope_ch"] = MeasurementEngine::getInstance().fastChannel();

  // Live WiFi status (same fields as /api/status)
  data["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  data["wifi_ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("");
  data["wifi_ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");

  String json;
  serializeJson(doc, json);
  _ws.textAll(json);
}

void PowerMeterWebServer::notifyAlarm(int channel, const char *message) {
  if (!_running) return;
  JsonDocument doc;
  doc["type"] = "alarm";
  doc["channel"] = channel;
  doc["message"] = message;
  String json;
  serializeJson(doc, json);
  _ws.textAll(json);
}

// ---- WebSocket Event Handler ----

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected\n", client->id());

    // Send initial status with recording states
    auto &recorder = DataRecorder::getInstance();
    auto &cfg = DeviceSettings::getInstance();
    JsonDocument doc;
    doc["type"] = "status";
    doc["any_recording"] = recorder.isAnyRecording();
    JsonArray rec_arr = doc["recording"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
      JsonObject r = rec_arr.add<JsonObject>();
      r["active"]  = recorder.isChannelRecording(i);
      r["elapsed"] = recorder.elapsedStr(i);
      r["samples"] = recorder.channelState(i).sample_count;
    r["last_file"] = recorder.channelState(i).last_file;
    }
    String json;
    serializeJson(doc, json);
    client->text(json);

  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected\n", client->id());
    // If this client owned the stream, tear it down NOW (close file, reset
    // state) — otherwise the file handle leaks and LittleFS runs out of
    // handles, breaking the next refresh.
    if (s_stream_ch >= 0 && s_stream_client == (int)client->id()) {
      streamAbort("disconnect");
    }
  } else if (type == WS_EVT_DATA) {
    // APP commands over WebSocket: {type:'cmd', cmd:..., ...}
    if (!data || !len) return;
    JsonDocument doc;
    if (deserializeJson(doc, (const char*)data, len)) return;
    String cmd = doc["cmd"] | "";
    JsonDocument ack;
    ack["type"] = "ack";
    ack["cmd"] = cmd;

    if (cmd == "record_start") {
      int ch = doc["ch"] | -1;
      String name = doc["name"] | "test";
      if (ch >= 0 && ch <= 3) {
        auto &rec = DataRecorder::getInstance();
        if (rec.isChannelRecording(ch)) rec.renameCurrent(ch, name.c_str());
        else ack["ok"] = rec.startChannel(ch, name.c_str());
        ack["ok"] = true;
      } else ack["ok"] = false;
    } else if (cmd == "record_stop") {
      int ch = doc["ch"] | -1;
      if (ch >= 0 && ch <= 3) {
        DataRecorder::getInstance().stopChannel(ch);
        ack["ok"] = true;
      } else ack["ok"] = false;
    } else if (cmd == "delete") {
      String fname = doc["file"] | "";
      if (fname.length() && fname.indexOf('/') < 0 && fname.indexOf('\\') < 0) {
        String path = "/data/" + fname;
        ack["ok"] = LittleFS.exists(path) ? LittleFS.remove(path) : false;
      } else ack["ok"] = false;
    } else if (cmd == "get_settings") {
      settingsToJson(ack);   // merge settings into the standard ack payload
      ack["ok"] = true;
    } else if (cmd == "record_all") {
      int ch = doc["ch"] | -1;
      int offset = doc["offset"] | 0;
      int limit = doc["limit"] | 300;
      if (ch < 0 || ch > 3) { ack["ok"] = false; }
      else {
        if (offset < 0) offset = 0;
        if (limit < 1) limit = 1;
        if (limit > 500) limit = 500;
        // Pause broadcasts so this (large) response isn't stuck in the send queue
        ws_send_busy_until = millis() + 8000;
        JsonArray arr = ack["data"].to<JsonArray>();
        channelRecordAllToJson(arr, ch, offset, limit);
        ack["ok"] = true;
      }
    } else if (cmd == "files") {
      auto &rec = DataRecorder::getInstance();
      JsonArray arr = ack["data"].to<JsonArray>();
      rec.listFilesToJson(arr);
      ack["ok"] = true;
    } else if (cmd == "storage") {
      ack["total_kb"] = LittleFS.totalBytes() / 1024;
      ack["used_kb"]  = LittleFS.usedBytes() / 1024;
      ack["free_kb"]  = (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024;
      ack["ok"] = true;
    } else if (cmd == "restart") {
      ack["ok"] = true;
      String ack_json;
      serializeJson(ack, ack_json);
      client->text(ack_json);
      delay(100);
      ESP.restart();
      return;
    } else if (cmd == "download_start") {
      String fname = doc["file"] | "";
      bool ok = false;
      if (fname.length() && fname.indexOf('/') < 0 && fname.indexOf('\\') < 0) {
        String path = "/data/" + fname;
        if (LittleFS.exists(path)) {
          File f = LittleFS.open(path);
          if (f) {
            uint8_t buf[512];
            size_t rd;
            while ((rd = f.read(buf, sizeof(buf))) > 0) {
              client->binary(buf, rd);
            }
            f.close();
            ok = true;
          }
        }
      }
      client->text(ok ? "{\"type\":\"dl_done\"}"
                      : "{\"type\":\"dl_fail\",\"error\":\"file not found\"}");
      return;
    } else if (cmd == "stream_start") {
      int ch = doc["ch"] | -1;
      if (ch < 0 || ch > 3) { ack["ok"] = false; }
      else {
        auto &rec = DataRecorder::getInstance();
        const char *fname = rec.currentFilename(ch);
        bool ok = false;
        if (fname && fname[0] && LittleFS.exists(fname)) {
          s_stream_file = LittleFS.open(fname);
          if (s_stream_file) {
            s_stream_ch = ch;
            s_stream_client = client->id();
            s_stream_off = doc["offset"] | 0;   // resume point (sample index)
            s_stream_n_file = s_stream_file.size() / sizeof(SampleBin);
            s_stream_total = s_stream_n_file + rec.bufferCount(ch);
            if (s_stream_off > s_stream_total) s_stream_off = s_stream_total;
            if (s_stream_off < s_stream_n_file) {
              s_stream_file.seek((size_t)s_stream_off * sizeof(SampleBin));
            }
            s_stream_last = 0;  // force first chunk immediately
            s_stream_fail = 0;
            s_done_fail = 0;
            ws_send_busy_until = 0xFFFFFFFF;  // pause broadcasts for the whole stream (no send-queue contention)
            ok = true;
          }
        }
        if (!ok) {
          if (s_stream_file) s_stream_file.close();
          s_stream_ch = -1;
          s_stream_client = -1;
          ws_send_busy_until = 0;
        }
        ack["ok"] = ok;
        ack["total"] = ok ? s_stream_total : 0;
        ack["start_ts"] = ok ? (uint32_t)rec.channelState(ch).start_ts : 0;
      }
    } else if (cmd == "settings") {
      // Same params as POST /api/settings
      auto &cfg = DeviceSettings::getInstance();
      if (doc["device_name"]) { cfg.setDeviceName(doc["device_name"]); }
      if (doc["ap_password"]) { cfg.setAPPassword(doc["ap_password"]); }
      if (doc["wifi_ssid"])   { cfg.setWiFi(doc["wifi_ssid"], doc["wifi_password"] | cfg.wifiPassword()); }
      if (doc["temp_unit"])   { cfg.setTempUnit(((String)(const char*)doc["temp_unit"])[0]); }
      if (doc["tz_offset"])   { cfg.setTzOffset(doc["tz_offset"]); }
      if (doc["sample_interval_ms"]) { cfg.setSampleIntervalMs(doc["sample_interval_ms"]); }
      if (doc["amb_temp_offset"]) { cfg.setAmbTempOffset(doc["amb_temp_offset"]); }
      if (doc["stop_ch"]) {
        int ch = doc["stop_ch"];
        cfg.setStopCond(ch,
          ((String)(const char*)(doc["stop_en"] | "0")) == "1",
          doc["stop_v"] | 0.0f,
          doc["stop_mA"] | 0.0f,
          doc["stop_min"] | 0,
          ((String)(const char*)(doc["stop_fall"] | "1")) == "1");
      }
      cfg.save();
      ack["ok"] = true;
    } else {
      ack["ok"] = false;
      ack["error"] = "unknown cmd";
    }
    // Echo req_id so the client can match this ack to its pending request
    ack["req_id"] = doc["req_id"] | 0;
    String ack_json;
    serializeJson(ack, ack_json);
    client->text(ack_json);
  }
}

// ---- REST API Routes ----

static void handleStatus(AsyncWebServerRequest *request) {
  auto &me = MeasurementEngine::getInstance();
  auto &recorder = DataRecorder::getInstance();
  auto &cfg = DeviceSettings::getInstance();
  auto &snap = me.getLatest();

  JsonDocument doc;
  doc["timestamp"] = time(nullptr);  // real epoch time
  doc["any_recording"] = recorder.isAnyRecording();
  doc["ambient_temp_C"] = snap.env.ambient_temp_C;
  doc["temp_unit"] = String(cfg.tempUnit());
  doc["scope_ch"] = me.fastChannel();

  // Live WiFi status (updates on reconnect/IP change)
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi_ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("");
  doc["wifi_ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");

  JsonArray ch_arr = doc["channels"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject ch = ch_arr.add<JsonObject>();
    channelToJson(ch, snap.channels[i]);
    ch["name"]  = cfg.channelName(i);
  }

  // Per-channel recording states
  JsonArray rec_arr = doc["recording"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject r = rec_arr.add<JsonObject>();
    r["active"]  = recorder.isChannelRecording(i);
    r["elapsed"] = recorder.elapsedStr(i);
    r["samples"] = recorder.channelState(i).sample_count;
    r["last_file"] = recorder.channelState(i).last_file;
    r["name"]    = recorder.channelState(i).name;
  }

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// GET  /api/settings
// POST /api/settings
static void handleSettings(AsyncWebServerRequest *request) {
  auto &cfg = DeviceSettings::getInstance();

  if (request->method() == HTTP_POST) {
    // Parse JSON body
    if (request->contentLength() > 0) {
      // Read body as string
      String body;
      // ESPAsyncWebServer doesn't have a simple body reader for raw string
      // Use params
    }

    // Try to get params from form or JSON
    bool updated = false;
    bool ap_changed = false;

    if (request->hasParam("device_name", true)) {
      cfg.setDeviceName(request->getParam("device_name", true)->value().c_str());
      updated = true;
    }
    if (request->hasParam("ap_password", true)) {
      String newPass = request->getParam("ap_password", true)->value();
      // ESP32 softAP requires password >= 8 chars or empty (open)
      if (newPass.length() > 0 && newPass.length() < 8) {
        JsonDocument err;
        err["ok"] = false;
        err["error"] = "AP password must be >= 8 characters or empty";
        String json;
        serializeJson(err, json);
        request->send(400, "application/json", json);
        return;
      }
      cfg.setAPPassword(newPass.c_str());
      updated = true;
      ap_changed = true;
    }
    if (request->hasParam("wifi_ssid", true)) {
      cfg.setWiFi(
        request->getParam("wifi_ssid", true)->value().c_str(),
        cfg.wifiPassword()
      );
      updated = true;
    }
    if (request->hasParam("wifi_password", true)) {
      cfg.setWiFi(
        cfg.wifiSSID(),
        request->getParam("wifi_password", true)->value().c_str()
      );
      updated = true;
    }
    if (request->hasParam("sample_interval_ms", true)) {
      cfg.setSampleIntervalMs(
        request->getParam("sample_interval_ms", true)->value().toInt()
      );
      MeasurementEngine::getInstance().setSampleInterval(cfg.sampleIntervalMs());
      updated = true;
    }
    if (request->hasParam("temp_unit", true)) {
      String unit = request->getParam("temp_unit", true)->value();
      cfg.setTempUnit(unit.charAt(0));
      updated = true;
    }
    if (request->hasParam("amb_temp_offset", true)) {
      cfg.setAmbTempOffset(request->getParam("amb_temp_offset", true)->value().toFloat());
      updated = true;
    }
    if (request->hasParam("stop_ch", true)) {
      int ch = request->getParam("stop_ch", true)->value().toInt();
      cfg.setStopCond(ch,
        request->hasParam("stop_en", true)  && request->getParam("stop_en", true)->value() == "1",
        request->hasParam("stop_v", true)   ? request->getParam("stop_v", true)->value().toFloat() : 0,
        request->hasParam("stop_mA", true)  ? request->getParam("stop_mA", true)->value().toFloat() : 0,
        request->hasParam("stop_min", true) ? request->getParam("stop_min", true)->value().toInt() : 0,
        request->hasParam("stop_fall", true)? request->getParam("stop_fall", true)->value() == "1" : true
      );
      updated = true;
    }
    if (request->hasParam("rotation", true)) {
      uint16_t r = request->getParam("rotation", true)->value().toInt();
      if (g_lv_disp) {
        lv_display_set_rotation(g_lv_disp, (lv_display_rotation_t)(r / 90));
        lv_display_set_render_mode(g_lv_disp, LV_DISPLAY_RENDER_MODE_FULL);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(NULL);
        lv_display_set_render_mode(g_lv_disp, LV_DISPLAY_RENDER_MODE_PARTIAL);
      }
      updated = true;
    }
    if (request->hasParam("tz_offset", true)) {      cfg.setTzOffset(
        request->getParam("tz_offset", true)->value().toInt()
      );
      updated = true;
    }

    if (updated) {
      cfg.save();
    }

    JsonDocument doc;
    doc["ok"] = updated;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
    return;
  }

  // GET: return all settings
  JsonDocument doc;
  settingsToJson(doc);
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// Shared: fill a JSON doc with all settings (GET /api/settings + WS get_settings)
static void settingsToJson(JsonDocument &doc) {
  auto &cfg = DeviceSettings::getInstance();
  doc["device_name"]    = cfg.deviceName();
  doc["ap_password"]    = "***";
  doc["wifi_ssid"]      = cfg.wifiSSID();
  doc["wifi_password"]  = "***";
  doc["sample_interval_ms"] = cfg.sampleIntervalMs();
  doc["temp_unit"] = String(cfg.tempUnit());
  doc["tz_offset"] = cfg.tzOffset();
  doc["rotation"] = cfg.rotation();
  doc["version"] = FIRMWARE_VERSION;
  doc["amb_temp_offset"] = cfg.ambTempOffset();

  // Stop conditions per channel
  JsonArray sc_arr = doc["stop_cond"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    auto &sc = cfg.stopCond(i);
    JsonObject o = sc_arr.add<JsonObject>();
    o["en"] = sc.enabled;
    o["v"]  = sc.voltage_threshold_V;
    o["mA"] = sc.current_threshold_mA;
    o["min"]= sc.max_duration_min;
    o["fall"]= sc.falling_edge;
  }

  // Computed AP SSID: device_name + "-" + last 3 bytes of MAC
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  char ap_name[40];
  snprintf(ap_name, sizeof(ap_name), "%s-%02X%02X%02X",
           cfg.deviceName(), mac[3], mac[4], mac[5]);
  doc["ap_ssid"] = ap_name;

  // Current IPs
  if (WiFi.getMode() & WIFI_MODE_AP) {
    doc["ap_ip"] = WiFi.softAPIP().toString();
  } else {
    doc["ap_ip"] = "";
  }
  if (WiFi.status() == WL_CONNECTED) {
    doc["wifi_ip"] = WiFi.localIP().toString();
  } else {
    doc["wifi_ip"] = "";
  }

  JsonArray ch_arr = doc["channel_names"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    ch_arr.add(cfg.channelName(i));
  }
}

// POST /api/channel/<ch>/record/start
static void handleChannelRecordStart(AsyncWebServerRequest *request) {
  Serial.println("[REC] START handler entered");
  int ch = parseChannelFromUrl(request->url());
  Serial.printf("[REC] ch=%d\n", ch);
  if (ch < 0 || ch > 3) {
    request->send(400, "application/json", "{\"ok\":false}");
    return;
  }
  String name = "test";
  if (request->hasParam("name", true)) {
    name = request->getParam("name", true)->value();
  }
  Serial.printf("[REC] name=%s\n", name.c_str());
  Serial.printf("[REC] LittleFS free=%d\n", LittleFS.totalBytes()-LittleFS.usedBytes());
  auto &rec = DataRecorder::getInstance();
  bool ok;
  if (rec.isChannelRecording(ch)) {
    // Already recording — rename the in-progress session
    rec.renameCurrent(ch, name.c_str());
    ok = true;
    Serial.println("[REC] renamed in-progress recording");
  } else {
    Serial.println("[REC] calling startChannel...");
    ok = rec.startChannel(ch, name.c_str());
    Serial.printf("[REC] startChannel returned %d\n", ok);
  }
  JsonDocument doc;
  doc["ok"] = ok;
  doc["channel"] = ch;
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
  Serial.println("[REC] handler done");
}

// GET /api/channel/<ch>/record/all — all data from .dat + buffer
// Shared: fills a JsonArray with samples [offset, offset+limit)
static void channelRecordAllToJson(JsonArray &arr, int ch, int offset, int limit) {
  auto &rec = DataRecorder::getInstance();

  // Layout: [ file samples (n) ][ ring buffer samples (cnt) ]
  const char *fname = rec.currentFilename(ch);
  int n = 0;
  if (fname && fname[0] && LittleFS.exists(fname)) {
    File f = LittleFS.open(fname);
    if (f) { n = f.size() / sizeof(SampleBin); f.close(); }
  }
  uint16_t cnt = rec.bufferCount(ch);
  int total = n + cnt;

  // 1. File portion
  if (offset < n) {
    File f = LittleFS.open(fname);
    if (f) {
      f.seek((size_t)offset * sizeof(SampleBin));
      int end = min(offset + limit, n);
      for (int i = offset; i < end; i++) {
        SampleBin b;
        if (f.read((uint8_t *)&b, sizeof(SampleBin)) != sizeof(SampleBin)) break;
        JsonObject o = arr.add<JsonObject>();
        o["t"] = b.timestamp;
        o["V"] = b.bus_voltage_V;
        o["A"] = b.current_A;
        o["W"] = b.power_W;
        o["C"] = b.channel_temp_C;
      }
      f.close();
    }
  }

  // 2. Ring buffer portion (samples after the file part)
  int bufStart = max(0, offset - n);
  int bufEnd = min((int)cnt, bufStart + (limit - (int)arr.size()));
  uint16_t head = rec.bufferHead(ch);
  for (int i = bufStart; i < bufEnd; i++) {
    int idx = (head - cnt + i + 60) % 60;
    const SampleBin &b = rec.bufferData(ch)[idx];
    JsonObject o = arr.add<JsonObject>();
    o["t"] = b.timestamp;
    o["V"] = b.bus_voltage_V;
    o["A"] = b.current_A;
    o["W"] = b.power_W;
    o["C"] = b.channel_temp_C;
  }
}

static void handleChannelRecordAll(AsyncWebServerRequest *request) {
  int ch = parseChannelFromUrl(request->url());
  if (ch < 0 || ch > 3) { request->send(400, "application/json", "[]"); return; }

  // Pagination: offset = sample index (0-based, across file + ring buffer)
  int offset = 0, limit = 300;
  if (request->hasParam("offset")) offset = request->getParam("offset")->value().toInt();
  if (request->hasParam("limit"))  limit  = request->getParam("limit")->value().toInt();
  if (offset < 0) offset = 0;
  if (limit < 1) limit = 1;
  if (limit > 500) limit = 500;

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  channelRecordAllToJson(arr, ch, offset, limit);

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// POST /api/channel/<ch>/record/stop
static void handleChannelRecordStop(AsyncWebServerRequest *request) {
  int ch = parseChannelFromUrl(request->url());
  if (ch < 0 || ch > 3) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
    return;
  }

  auto &recorder = DataRecorder::getInstance();
  recorder.stopChannel(ch);

  JsonDocument doc;
  doc["ok"] = true;
  doc["channel"] = ch;
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// POST /api/channel/<ch>/name
static void handleChannelRename(AsyncWebServerRequest *request) {
  int ch = parseChannelFromUrl(request->url());
  if (ch < 0 || ch > 3) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
    return;
  }

  String name;
  if (request->hasParam("name", true)) {
    name = request->getParam("name", true)->value();
  }

  if (name.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"name required\"}");
    return;
  }

  // Channel names are fixed (CH1-CH4) — nothing to save
  (void)ch; (void)name;

  JsonDocument doc;
  doc["ok"] = true;
  doc["channel"] = ch;
  doc["name"] = name;
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

static void handleBuzzerTest(AsyncWebServerRequest *request) {
  // Non-blocking beep via the global buzzer instance
  extern Buzzer g_buzzer;
  g_buzzer.beep(2400, 150);

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Buzzer: single beep";
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// GET /api/sensors �?returns cached sensor status from boot
static void handleSensors(AsyncWebServerRequest *request) {
  auto &me = MeasurementEngine::getInstance();
  JsonDocument doc;
  JsonArray ina = doc["ina226"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject ch = ina.add<JsonObject>();
    ch["channel"] = i + 1;
    ch["addr"] = String("0x") + String(i == 0 ? 40 : i == 1 ? 41 : i == 2 ? 44 : 45, HEX);
    ch["ok"] = me.isINA226Connected(i);
  }
  doc["ads1115"] = me.isADS1115Connected();
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// GET /api/logs
static void handleLogs(AsyncWebServerRequest *request) {
  const char *s = WebLog::getInstance().loadAll();
  request->send(200, "text/plain; charset=utf-8", s);
  free((void*)s);
}

// GET /api/files
static void handleListFiles(AsyncWebServerRequest *request) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  File root = LittleFS.open("/data");
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      JsonObject file_obj = arr.add<JsonObject>();
      String name = String(f.name());
      if(name.startsWith("/data/")) name = name.substring(6); // strip /data/ prefix
      file_obj["name"] = name;
      file_obj["size"] = f.size();
      f = root.openNextFile();
    }
    root.close();
  }

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// GET /api/download?file=xxx.csv
static void handleDownload(AsyncWebServerRequest *request) {
  if (!request->hasParam("file")) {
    request->send(400, "text/plain", "Missing file param");
    return;
  }

  String fname = request->getParam("file")->value();
  if(fname.indexOf("..")>=0||fname.indexOf('/')>=0||fname.indexOf('\\')>=0){request->send(403);return;}
  String filename = "/data/" + fname;
  if (!LittleFS.exists(filename)) {
    request->send(404, "text/plain", "File not found");
    return;
  }

  AsyncWebServerResponse *resp = request->beginResponse(LittleFS, filename, "text/csv");
  resp->addHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
  request->send(resp);
}

// POST /api/delete?file=xxx.csv
static void handleDeleteFile(AsyncWebServerRequest *request) {
  if (!request->hasParam("file")) {
    Serial.println("[API] DELETE: missing file param");
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing file\"}");
    return;
  }

  String fname = request->getParam("file")->value();
  Serial.printf("[API] DELETE: file=%s\n", fname.c_str());
  if(fname.indexOf("..")>=0||fname.indexOf('/')>=0||fname.indexOf('\\')>=0){request->send(403);return;}
  String filename = "/data/" + fname;
  bool ok = DataRecorder::getInstance().deleteFile(filename.c_str());
  Serial.printf("[API] DELETE: %s -> %s\n", filename.c_str(), ok ? "OK" : "FAIL");
  request->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// POST /api/restart
static void handleRestart(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Rebooting...";
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
  delay(200);
  ESP.restart();
}

// POST /api/wifi �?save + try connect while keeping AP alive
static void handleWifiConfig(AsyncWebServerRequest *request) {
  if (!request->hasParam("ssid", true)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing ssid\"}");
    return;
  }

  String ssid = request->getParam("ssid", true)->value();
  String pass = request->hasParam("password", true) ?
                request->getParam("password", true)->value() : "";

  // Save credentials
  DeviceSettings::getInstance().setWiFi(ssid.c_str(), pass.c_str());
  DeviceSettings::getInstance().save();

  // Try to connect without killing AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  // Quick check �?don't block long
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(300);
    attempts++;
  }

  JsonDocument doc;
  if (WiFi.status() == WL_CONNECTED) {
    ntpSync();
    doc["ok"] = true;
    doc["ip"] = WiFi.localIP().toString();
    doc["message"] = "Connected";
  } else {
    doc["ok"] = true;
    doc["ip"] = "";
    doc["message"] = "Saved. Connection failed �?check SSID/password.";
  }
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// ---- Setup ----

void PowerMeterWebServer::setupRoutes() {
  // Global CORS headers for local dashboard
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
  // Add CORS headers to all responses
  _server.onNotFound([](AsyncWebServerRequest *r){
    r->send(404);
  });
  // Dashboard HTML
  _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("[WS] GET / �?serving dashboard");
    request->send(200, "text/plain", "4CH Power Meter API OK");
  });

  // Health check
  _server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "pong");
  });

  // API routes
  _server.on("/api/status", HTTP_GET, handleStatus);
  _server.on("/api/settings", HTTP_GET, handleSettings);
  _server.on("/api/settings", HTTP_POST, handleSettings);
  _server.on("/api/files", HTTP_GET, handleListFiles);
  _server.on("/api/storage", HTTP_GET, handleStorage);
  _server.on("/api/download", HTTP_GET, handleDownload);
  _server.on("/api/delete", HTTP_POST, handleDeleteFile);
  _server.on("/api/wifi", HTTP_POST, handleWifiConfig);
  _server.on("/api/buzzer/test", HTTP_POST, handleBuzzerTest);
  _server.on("/api/i2c/scan", HTTP_GET, handleSensors);
  _server.on("/api/logs", HTTP_GET, handleLogs);
  _server.on("/api/restart", HTTP_POST, handleRestart);

  // Channel routes — registered in a loop (4 channels × 4 actions)
  for (int ch = 0; ch < 4; ch++) {
    char p[40];
    snprintf(p, sizeof(p), "/api/channel/%d/record/start", ch);
    _server.on(p, HTTP_POST, handleChannelRecordStart);
    snprintf(p, sizeof(p), "/api/channel/%d/record/stop", ch);
    _server.on(p, HTTP_POST, handleChannelRecordStop);
    snprintf(p, sizeof(p), "/api/channel/%d/record/data", ch);
    snprintf(p, sizeof(p), "/api/channel/%d/record/all", ch);
    _server.on(p, HTTP_GET, handleChannelRecordAll);
  }
  _server.on("/api/channel/0/name",         HTTP_POST, handleChannelRename);
  _server.on("/api/channel/1/name",         HTTP_POST, handleChannelRename);
  _server.on("/api/channel/2/name",         HTTP_POST, handleChannelRename);
  _server.on("/api/channel/3/name",         HTTP_POST, handleChannelRename);

  // CORS preflight
  _server.on("^.*$", HTTP_OPTIONS, [](AsyncWebServerRequest *r){
    AsyncWebServerResponse *resp = r->beginResponse(204);
    resp->addHeader("Access-Control-Allow-Origin","*");
    resp->addHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers","*");
    r->send(resp);
  });

  // 404 handler
  _server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not Found");
  });
}

void PowerMeterWebServer::setupWebSocket() {
  _ws.onEvent(onWsEvent);
  _server.addHandler(&_ws);
}
