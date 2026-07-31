# 四通道功率计 — 固件需求规格说明书

> **文档版本**: 1.0 | **对应固件版本**: v0.2.4  
> **产品型号**: O3601V10 | **最后更新**: 2025-07

---

## 1. 引言

### 1.1 产品概述

四通道功率计（4-Channel Power Meter）是一款用于同时测量 4 路直流电源通道的嵌入式测试仪器。每通道独立采集总线电压、分流电压、负载电流、负载功率和通道温度，并提供环境温度监测。数据通过设备屏幕和 Web 仪表盘实时展示，支持独立通道的长时间 CSV 录制。

### 1.2 适用场景

- 多路电源模块效率测试
- 电池充放电多通道监控
- 电子负载多通道功率分析
- 产品质量检测台位

### 1.3 术语定义

| 术语 | 说明 |
|------|------|
| INA226 | TI 公司电流/功率监视器 IC，I2C 接口，支持高/低侧测量 |
| ADS1115 | TI 公司 16 位 4 通道 ΔΣ ADC，I2C 接口 |
| B3950 NTC | B 值为 3950K、标称 10KΩ 的负温度系数热敏电阻 |
| Kelvin 4线 | 开尔文四线检测法，分离激励线和感应线以消除引线电阻 |
| EN0 | 通道主开关控制信号（高有效），控制 MOSFET 通断 |
| LittleFS | ESP32 嵌入式文件系统，用于存储 CSV 录制文件 |
| NVS | Non-Volatile Storage，ESP32 非易失性键值存储 |

---

## 2. 功能需求

### FR-1: 多通道同步测量

**FR-1.1** 系统应同时采集 4 个独立通道的以下物理量：

| 物理量 | 传感器 | 单位 | 精度要求 |
|--------|--------|------|----------|
| 总线电压 | INA226 内置 ADC | V | ±0.1% (±1 LSB) |
| 分流电压 | INA226 内置 ADC | mV | ±10μV |
| 电流 | INA226 计算值 | mA | 取决于分流电阻精度 |
| 功率 | INA226 计算值 | mW | 取决于 V×I 精度 |
| 通道温度 | B3950 NTC (ADS1115) | °C | ±1°C |
| 环境温度 | B3950 NTC (ESP32 ADC) | °C | ±1°C |

**FR-1.2** 所有通道应在一次采样周期内完成读取（时间戳一致）。

**FR-1.3** 当某通道 INA226 未连接时，该通道数值应显示为 `---`，不应阻塞其他通道的正常工作。

### FR-2: 采样策略

**FR-2.1** 非录制状态：系统以 1Hz 频率采样，仅用于屏幕刷新。

**FR-2.2** 录制状态：系统以用户配置的间隔采样（范围 100ms ~ 60000ms，默认 1000ms）。

**FR-2.3** INA226 连接状态检测采用轮询方式（每次采样轮询 1 个芯片，4 次完成一轮），避免 I2C 总线冲突。

**FR-2.4** 环境温度应取 16 次 ADC 平均值以降低噪声。

### FR-3: 数据录制与存储

**FR-3.1** 支持 4 个通道独立录制，互不影响。

**FR-3.2** 录制启动时，系统应为该通道创建 CSV 文件，路径格式: `/data/CH{N}_{sanitized_name}.csv`。

**FR-3.3** 测试名称仅保留字母数字、连字符和下划线，空格转为下划线，空名称默认为 `record`。

**FR-3.4** CSV 文件需包含表头行: `timestamp_ms,Voltage_V,Current_mA,Power_mW,Temp_C,Ambient_TempC`。

**FR-3.5** 每 10 条样本执行一次文件 flush，防止意外断电数据丢失。

**FR-3.6** 录制停止时需 flush 并关闭文件，输出样本计数日志。

**FR-3.7** 支持通过 Web API 列出、下载、删除录制文件。

### FR-4: 报警与告警

**FR-4.1** 每通道支持配置最多 2 个独立报警条件。

**FR-4.2** 报警条件类型应包括:
- `COND_CURRENT_BELOW` — 电流低于阈值
- `COND_CURRENT_ABOVE` — 电流高于阈值
- `COND_VOLTAGE_BELOW` — 电压低于阈值
- `COND_VOLTAGE_ABOVE` — 电压高于阈值
- `COND_POWER_BELOW` — 功率低于阈值
- `COND_POWER_ABOVE` — 功率高于阈值
- `COND_TEMP_BELOW` — 温度低于阈值
- `COND_TEMP_ABOVE` — 温度高于阈值

**FR-4.3** 报警需支持持续时间消抖（`duration_ms`），条件需持续满足指定时长后才触发。

**FR-4.4** 报警触发动作: 蜂鸣器播放 3000Hz × 3 次警告音 + WebSocket 推送 alarm 消息。

**FR-4.5** 报警一旦触发即锁存（`triggered=true`），不会重复触发同一条件。

### FR-5: WiFi 连接

**FR-5.1** 上电默认启动 AP 模式，SSID 格式为 `{device_name}-{MAC后3字节大写十六进制}`。

**FR-5.2** 默认 AP 密码为 `12345678`，可通过 Web API 修改。

**FR-5.3** 若配置的 AP 密码长度在 1~7 字符之间（ESP32 不支持），系统应自动回退到默认 SSID 和密码。

**FR-5.4** 支持 STA 模式连接外部路由器，同时保持 AP 运行（WIFI_AP_STA 双模）。

**FR-5.5** STA 连接重试策略: 前 5 次间隔 5s，之后间隔 30s，最多 10 次后放弃。

**FR-5.6** STA 连接成功后自动同步 NTP 时间（UTC+8），并保存 WiFi 凭证到 NVS。

### FR-6: Web 服务

**FR-6.1** 设备应在 80 端口提供 HTTP 服务。

**FR-6.2** 根路径 `/` 返回内嵌的仪表盘 HTML 页面。

**FR-6.3** `/ws` 提供 WebSocket 连接，每 500ms 广播一次 `measurement` 消息。

**FR-6.4** WebSocket 客户端连接时应立即收到 `status` 消息（含当前录制状态）。

**FR-6.5** API 响应格式统一为 JSON。

**FR-6.6** 支持设备远程重启（`POST /api/restart`）。

### FR-7: 人机交互

**FR-7.1** LVGL 屏幕（320×170）主页面应展示:
- 顶栏: 当前时间（HH:MM）、录制状态指示灯（REC 红色闪烁 / IDLE 灰色）、环境温度
- 4 通道卡片: 每卡片显示 V / mA / W / °C，已连接通道标题栏为橙色，未连接为灰色
- 底栏: WiFi SSID（左）、IP 地址（右）

**FR-7.2** 录制状态指示: "REC" 文字红色 + 状态点红/暗交替闪烁。

**FR-7.3** 按键映射:
- Button A (GPIO0) 短按 = UP (通道上), 长按 = BACK (返回)
- Button B (GPIO14) 短按 = DOWN (通道下), 长按 = ENTER (确认)

**FR-7.4** 蜂鸣器启动提示音: do(523Hz 200ms) → re(587Hz 200ms) → mi(659Hz 200ms)。

**FR-7.5** 若部分传感器未检测到，蜂鸣器播放 2 声短促警告音（1000Hz, 100ms on / 100ms off × 2）。

### FR-8: 配置持久化

**FR-8.1** 所有用户可配置参数应存储在 ESP32 NVS 分区（命名空间 `settings`）。

**FR-8.2** 配置项包括: 设备名称、AP SSID/密码、WiFi STA SSID/密码、4 通道名称、采样间隔、温度单位。

**FR-8.3** 固件启动时自动从 NVS 加载，修改后需显式调用 `save()` 写入。

---

## 3. 非功能需求

### NFR-1: 可靠性

- 固件应对 INA226 和 ADS1115 未连接的情况有容错处理
- LittleFS 挂载失败不应阻止测量和 Web 服务运行
- HTTP 请求处理不应阻塞主循环（使用异步 Web 服务器）

### NFR-2: 性能

- I2C 总线频率: 400kHz（Fast Mode）
- WebSocket 广播间隔: 500ms
- 录制 CSV flush 间隔: 每 10 条样本
- 主循环单次迭代应在 10ms 内完成（含 5ms delay）

### NFR-3: 可维护性

- 驱动层与应用层分离（drivers/ vs apps/）
- 使用单例模式管理全局资源（MeasurementEngine, DataRecorder, DeviceSettings, PowerMeterWebServer）
- pin_config.h 集中管理所有硬件引脚和 I2C 地址定义

### NFR-4: 安全性

- AP 密码长度校验（≥8 或空）
- 文件操作限制在 `/data/` 目录
- 无公网暴露（默认 AP 模式，局域网内访问）

---

## 4. 技术规格

### 4.1 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | ESP32-S3R8 (T-Display-S3) |
| Flash | 16MB |
| PSRAM | 8MB (Octal) |
| 显示屏 | ST7789 1.9" 320×170, 8-bit 并口 |
| 输入 | 2× 物理按键 (GPIO0, GPIO14) |

### 4.2 传感器规格

| 传感器 | 型号 | 接口 | 地址 | 精度 |
|--------|------|------|------|------|
| 电流检测 | INA226 | I2C | 0x40/0x41/0x44/0x45 | 16-bit ΔΣ |
| 分流电阻 | 2512 SMD | — | — | 0.5mΩ |
| ADC | ADS1115IDGS | I2C | 0x48 | 16-bit, ±2.048V 默认 |
| NTC | B3950 10K | 模拟分压 | ADS1115 CH0-3 + GPIO1 | ±1% |

### 4.3 电气特性

| 参数 | 值 | 备注 |
|------|-----|------|
| 总线电压范围 | 0 ~ 36V | INA226 规范 |
| 电流范围 | 0 ~ 80A | 0.5mΩ 分流时 |
| 分流满量程电压 | ±81.92mV | INA226 默认 |
| 通道开关 MOSFET | AON7544 (N-ch, 30V) | O3602V10 子板 |
| 通道 EN0 电平 | 3.3V LVCMOS | 高有效 |
| NTC 分压参考 | 3.3V | 10K 上拉电阻 |

### 4.4 软件架构

```
┌─────────────────────────────────────────────────┐
│                    loop()                        │
│  framework_loop → measurement.update →          │
│  buzzer.update → web_server.update →            │
│  web broadcast (500ms) → alarm check            │
├─────────────────────────────────────────────────┤
│  apps/                        drivers/           │
│  ┌──────────────┐    ┌───────────────────┐      │
│  │ Measurement  │───▶│ INA226 (×4)       │      │
│  │ Engine       │    │ ADS1115 (×1)      │      │
│  ├──────────────┤    │ NTC_B3950          │      │
│  │ DataRecorder │    │ ChannelControl     │      │
│  │ Settings     │    │ Buzzer             │      │
│  │ PowerMeterApp│    │ Button             │      │
│  └──────────────┘    └───────────────────┘      │
├─────────────────────────────────────────────────┤
│  www/               framework/                   │
│  ┌──────────────┐  ┌────────────────────┐       │
│  │ WebServer    │  │ Framework (LVGL)   │       │
│  │ WebSocket    │  │ UI Components       │       │
│  │ REST API     │  │ App Manager         │       │
│  │ WebLog       │  │ Font/LVGL Port      │       │
│  └──────────────┘  └────────────────────┘       │
├─────────────────────────────────────────────────┤
│  Platform: ESP32-S3 / Arduino / FreeRTOS         │
│  Storage: NVS (settings) + LittleFS (CSV data)   │
└─────────────────────────────────────────────────┘
```

### 4.5 数据流

```
INA226 × 4 ──┐
ADS1115      ──▶ MeasurementEngine._sampleAll()
ESP32 ADC    ──┘         │
                         ▼
              MeasurementSnapshot (内存)
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
   PowerMeterApp   DataRecorder    WebServer
   (LVGL 刷新)     (LittleFS)     (WebSocket)
                     │
                     ▼
              /data/CH{N}_xxx.csv
```

### 4.6 CSV 数据格式

```csv
timestamp_ms,Voltage_V,Current_mA,Power_mW,Temp_C,Ambient_TempC
12345,5.0012,120.50,602.40,35.21,26.80
13345,4.9998,121.10,605.50,35.25,26.82
```

### 4.7 I2C 地址分配

| 设备 | 地址 | A0 | A1 |
|------|------|----|----|
| INA226 CH1 | 0x40 | GND | GND |
| INA226 CH2 | 0x41 | VCC | GND |
| INA226 CH3 | 0x44 | GND | VCC |
| INA226 CH4 | 0x45 | VCC | VCC |
| ADS1115 | 0x48 | GND | GND |

---

## 5. 接口定义

### 5.1 REST API 详细

#### GET /api/status

```json
{
  "timestamp": 12345678,
  "any_recording": true,
  "ambient_temp_C": 26.8,
  "channels": [
    {"V": 5.001, "mA": 120.5, "mW": 602.4, "temp_C": 35.2, "name": "CH1"},
    {"V": 3.301, "mA": 50.1,  "mW": 165.3, "temp_C": 32.1, "name": "CH2"},
    {"V": 12.0,  "mA": 2300,  "mW": 27600, "temp_C": 45.8, "name": "CH3"},
    {"V": 1.8,   "mA": 800.3, "mW": 1440,  "temp_C": 30.0, "name": "CH4"}
  ],
  "recording": [
    {"active": true,  "elapsed": "05:30", "samples": 330, "name": "battery_test"},
    {"active": false, "elapsed": "00:00", "samples": 0,   "name": ""},
    {"active": false, "elapsed": "00:00", "samples": 0,   "name": ""},
    {"active": false, "elapsed": "00:00", "samples": 0,   "name": ""}
  ]
}
```

#### GET /api/settings

```json
{
  "device_name": "4CH Power Meter",
  "ap_ssid": "4CH Power Meter-A1B2C3",
  "ap_password": "12345678",
  "ap_ip": "192.168.4.1",
  "wifi_ssid": "",
  "wifi_password": "",
  "wifi_ip": "",
  "sample_interval_ms": 1000,
  "temp_unit": "C",
  "channel_names": ["CH1", "CH2", "CH3", "CH4"]
}
```

#### POST /api/settings

接受 form-encoded 参数: `device_name`, `ap_password`, `wifi_ssid`, `wifi_password`, `sample_interval_ms`, `temp_unit`

返回:
```json
{"ok": true}
```

错误 (AP 密码太短):
```json
{"ok": false, "error": "AP password must be >= 8 characters or empty"}
```

#### POST /api/channel/{0-3}/record/start

参数: `?name=test_name`

```json
{"ok": true, "channel": 0, "name": "test_name"}
```

#### POST /api/channel/{0-3}/record/stop

```json
{"ok": true, "channel": 0}
```

#### GET /api/files

```json
[
  {"name": "CH1_battery_test.csv", "size": 45200},
  {"name": "CH3_load_test.csv", "size": 12800}
]
```

#### GET /api/download?file=CH1_battery_test.csv

返回 CSV 文件（Content-Type: `text/csv`）。

#### GET /api/i2c/scan

```json
{
  "ina226": [
    {"channel": 1, "addr": "0x40", "ok": true},
    {"channel": 2, "addr": "0x41", "ok": true},
    {"channel": 3, "addr": "0x44", "ok": false},
    {"channel": 4, "addr": "0x45", "ok": true}
  ],
  "ads1115": true
}
```

#### POST /api/channel/{0-3}/name

参数: `?name=NewName`
```json
{"ok": true, "channel": 0, "name": "NewName"}
```

#### POST /api/buzzer/test

```json
{"ok": true, "message": "Buzzer: single beep"}
```

#### POST /api/wifi

参数: `?ssid=MyWiFi&password=mypassword`
```json
{"ok": true, "ip": "192.168.1.100", "message": "Connected"}
```

#### POST /api/restart

```json
{"ok": true, "message": "Rebooting..."}
```

#### POST /api/delete?file=CH1_battery_test.csv

```json
{"ok": true}
```

#### GET /api/logs

```json
{"total": 42, "count": 20, "lines": ["[INIT] Buzzer OK", "..."]}
```

#### GET /ping

响应: `pong` (text/plain)
```

### 5.2 WebSocket 消息

服务器 → 客户端:

| type | 频率 | 说明 |
|------|------|------|
| `status` | 连接时 1次 | 当前录制状态 |
| `measurement` | 500ms | 4通道数据 + 环境温度 + 录制状态 |
| `alarm` | 触发时 | 报警通知 |

---

## 6. 构建与部署

### 6.1 开发环境

```ini
[env:4ch-power-meter]
platform = espressif32@6.0.1
board = lilygo-t-displays3
framework = arduino
board_build.partitions = default_16MB.csv
```

### 6.2 编译标志

```
-DLV_LVGL_H_INCLUDE_SIMPLE
-DARDUINO_USB_CDC_ON_BOOT=1
-DDISABLE_ALL_LIBRARY_WARNINGS
-DARDUINO_USB_MODE=1
-DARDUINO_OTA
```

### 6.3 外部依赖

| 库 | 用途 |
|----|------|
| `bblanchon/ArduinoJson@^7.2.2` | REST API / WebSocket JSON 序列化 |
| `me-no-dev/AsyncTCP@^3.2.5` | 异步 TCP 支持 |
| `me-no-dev/ESPAsyncWebServer@^3.2.3` | HTTP + WebSocket 服务 |
| LVGL v9 (本地 `lib/lvgl/`) | 图形界面渲染 |
| T-Display-S3 Arduino 框架 | 显示/触摸/电源管理 |

---

## 7. 附录

### A. 通道 EN0 引脚映射

| 通道 | EN0 GPIO | 备注 |
|------|----------|------|
| CHA (CH1) | GPIO17 | 经 O3601V10 J3 连接至 O3602V10 J6 |
| CHB (CH2) | GPIO21 | 经 O3601V10 J5 连接至 O3602V10 J6 |
| CHC (CH3) | GPIO3 | 经 O3601V10 J6 连接至 O3602V10 J6 |
| CHD (CH4) | GPIO12 | 经 O3601V10 J7 连接至 O3602V10 J6 |

EN0 = HIGH → 通道 MOSFET 导通，负载通电  
EN0 = LOW → 通道 MOSFET 关断，负载断电

### B. 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| v0.2.4 | 2025-07 | 当前版本：4通道独立录制、WebSocket 实时推送、报警系统 |
