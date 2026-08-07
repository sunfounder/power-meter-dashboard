# SunFounder 4CH 功率计 — 软件设计文档 (SDD)

> 版本：v1.0 ｜ 对应需求：docs/REQUIREMENTS.md v1.1
> 本文档描述系统设计（架构/模块/接口/数据/流程），与需求文档配套。

---

## 1. 系统架构

```
┌─────────────────────────────────────────────────────────┐
│ 固件 ESP32-S3 (firmware/)                                │
│  ┌─────────┐ ┌──────────┐ ┌────────────┐ ┌───────────┐  │
│  │ 测量引擎 │ │ 录制器    │ │ 网络服务    │ │ 屏幕 UI   │  │
│  │ measure  │ │ record    │ │ server/WS  │ │ screen    │  │
│  └────┬────┘ └────┬─────┘ └─────┬──────┘ └─────┬─────┘  │
│       │          │             │             │         │
│  ┌────▼──────────▼─────────────▼─────────────▼──────┐  │
│  │ 配置 config（NVS）+ 日志 log（WebLog/LittleFS）      │  │
│  └──────────────────────────────────────────────────┘  │
└──────────────┬───────────────────────────────┬─────────┘
               │ WS (主) / HTTP (Agent)         │
┌──────────────▼───────────────┐ ┌─────────────▼─────────┐
│ 网页仪表盘 web/（浏览器）       │ │ 桌面应用 app/（Electron）│
│ 单页 index.html + vendor      │ │ 内嵌网页副本 dashboard/│
│ (echarts/font-awesome 本地)   │ │ + 更新器               │
└──────────────────────────────┘ └───────────────────────┘
```

## 2. 固件模块设计

| 模块 | 文件 | 职责 |
|---|---|---|
| 测量引擎 | `src/app/measure.cpp/.h` | 轮询 INA226/ADS1115、快照、自动停止判定（10 样本去抖）、scopes |
| 录制器 | `src/app/record.cpp/.h` | 环形缓冲(60)、.dat 落盘、文件命名/清理、崩溃恢复、.done 标记 |
| 配置 | `src/app/config.cpp/.h` | NVS 存储（设备名/网络/采样/时区/温度/停止条件 blob） |
| 网络服务 | `src/net/server.cpp/.h` | AP/STA、NTP、HTTP REST、WS 命令队列 + 广播 |
| 日志 | `src/net/log.cpp/.h` | WebLog（LittleFS 环形日志，/api/logs） |
| 屏幕 | `src/app/screen.cpp/.h` | LVGL UI（顶栏/卡片/状态） |
| 蜂鸣器 | `src/hal/buzzer.cpp/.h` | LEDC 蜂鸣（标志驱动，loop 消费） |
| 主循环 | `src/main.cpp` | 初始化、NTP 重试、auto-resume、stream/download 推送、内存监控 |

### 2.1 关键机制

- **跨任务安全**：WS 事件（AsyncTCP 任务）只**入队命令**（s_cmd_pending），main loop 的 `processCmdQueue()` 单线程执行——消除流状态竞态（panic 根因）
- **AsyncTCP 加固**：事件队列 32→256（防 lwIP 线程 xQueueSend 无限阻塞）
- **发送背压**：`space() >= 整块` 才发送（防部分发送积压 WS 队列）
- **WiFi 省电**：`setSleep(false)`（降低 TX 延迟/ack timeout）

## 3. 数据设计

### 3.1 .dat 文件格式（24B/样本，packed）

```
uint32 timestamp   // epoch 秒（UTC）
float  bus_voltage_V
float  current_A
float  power_W
float  channel_temp_C
float  ambient_temp_C
```
- 正常停止 → 文件重命名 `xxx.dat` → `xxx.dat.done`（完成标记）
- 崩溃/断电 → 保持 `.dat`（开机 auto-resume 依据）
- 无头部，读取按 24B 步进

### 3.2 NVS 键（config.cpp）

```
dev_name / ap_pass / wifi_ssid / wifi_pass / sample_ms /
temp_unit / tz_offset / rotation / amb_toff / stop_cond(blob 4×StopCond)
```
- 通道名不存（固定 CH1-CH4）

### 3.3 WebLog（LittleFS /data/system.log）

- 环形（最近 ~16KB），HTTP `/api/logs` 读取
- 记录启动/网络/录制/流事件

## 4. 接口设计

### 4.1 WebSocket 协议（ws://ip/ws）

**设备 → 网页**（文本 JSON / 二进制）：

| 消息 | 内容 |
|---|---|
| `status`（连接时/ get_status 响应） | any_recording、recording[]（active/elapsed/samples/last_file）、incomplete[] |
| `measurement`（1s 广播） | timestamp、env、channels[]（V/A/W/C/connected）、recording[]、wifi 状态（connected/ssid/ip/rssi）、scope_ch |
| `ack`（命令响应） | cmd、ok、req_id、data（按命令） |
| 二进制帧 | 下载 .dat 块（download_start 后）→ `dl_done`/`dl_fail` 结束 |

**网页 → 设备**（命令，`{type:'cmd', cmd, req_id, ...}`）：

| 命令 | 参数 | 响应 |
|---|---|---|
| `get_settings` | - | 全量设置 |
| `set_settings`(→`settings`) | 任意设置字段 | ok |
| `get_status` | - | status 负载 |
| `record_start` | ch, name, resume_file? | ok |
| `record_stop` | ch | ok |
| `rename` | ch, name | ok |
| `files` | - | data[]（name/size，含 .dat/.done） |
| `delete` | file | ok |
| `storage` | - | total/used/free_kb |
| `record_all` | ch, offset, limit(≤500), file? | data[]（样本数组 t/V/A/W/C）+ total |
| `download_start` | file | ok → 二进制帧 → dl_done |
| `restart` | - | ok（重启） |
| `buzzer` | - | ok |

### 4.2 HTTP REST（Agent/AI 用，网页不用）

```
GET /ping → pong
GET /api/status          # 同 WS status（含 wifi_rssi）
GET /api/settings        # 全量设置
POST /api/settings       # 改设置（表单）
GET /api/files           # 文件列表
GET /api/storage         # 空间
GET /api/logs            # WebLog
GET /api/channel/<ch>/record/all?offset&limit&file   # 分页数据
POST /api/delete?file= /api/restart /api/buzzer
```

## 5. 关键流程

### 5.1 历史数据获取（网页分页）

```
刷新 → get_status（恢复状态）→ loadBufferData(ch)
  → 循环：record_all{offset, limit:200} → 累加 → 进度显示
  → 每页失败重试 3 次（WS 断后重连续传）
  → 收完 → 一次性 setOption 画图 + 详细表
```

### 5.2 崩溃恢复（auto-resume）

```
开机 → NTP 同步（或 90s 兜底）→ findIncomplete()
  → 扫描 /data：*.dat（无 .done）+ 文件名时间（48h 内 / NTP 未同步时全报）
  → resumeChannel：去掉尾部（无 magic）、start_time 回拨（含历史）、续写
```

### 5.3 自动停止（10 样本去抖）

```
每样本：条件不满足 → below_cnt=0（未录制不计数）
条件满足（电流<阈值 && 电压<阈值 && 时长>max && 下降沿判定）→ below_cnt++
below_cnt >= 10 → 停止该通道
```

### 5.4 更新流程（桌面应用）

```
检查更新 → GitHub Releases API → 版本对比
→ 后台下载 zip（进度）→ 本地存储 → 用户点击"更新"
→ 解压 → updater.bat 替换 → 重启 → 首次启动显示 changelog
```

## 6. 网页结构（web/index.html 单文件）

- 核心数据流：WS → processData → up()（数值）/ uc()（实时曲线）/ updateTables()（详细表）
- 图表：ECharts 5（4 通道 × 4 系列，large/progressive，Y 轴最小范围）
- 状态机：recState（0 IDLE / 1 录制 / 2 停止）
- 全局：lastData、charts[4]、dataStore[4]、recState[4]、maxTs[4]、absMode[4]

## 7. 桌面应用（app/）

- main.js：窗口（无边框）、IPC（min/max/close/devtools/save-log/更新）
- preload.js：contextBridge 暴露 API
- dashboard/：网页副本（发版时同步）

## 8. 部署与发布

- `scripts/release.ps1 <version>`：版本号 → 同步 web→app/dashboard → 打包 zip → GitHub Release（含 firmware.bin）
- zip 内直接是文件（无 win-unpacked 层级）

---

## 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-08-07 | 初始设计文档（架构/模块/数据/接口/流程） |
