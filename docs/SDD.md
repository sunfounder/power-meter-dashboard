# SunFounder 4CH 功率计 — 软件设计文档 (SDD)

> 版本：v1.1 ｜ 对应需求：docs/REQUIREMENTS.md v1.1
> 本文档描述系统设计（架构/模块/接口/数据/流程），与需求文档配套。
> 详细程度：接口/数据给出完整字段表，供实现与联调直接引用。

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

线程模型（固件）：
- **loop 任务**：测量采样、录制 flush、屏幕刷新、stream/download 推送、命令队列消费、NTP 重试
- **AsyncTCP 任务**：TCP/WS 事件（连接/断开/数据帧）→ 只入队命令，不碰共享状态
- **lwIP 线程**：TCP 栈（ACK/重传）——AsyncTCP 事件队列 256 防阻塞

---

## 2. 固件模块设计

### 2.1 模块总表

| 模块 | 文件 | 职责 | 关键状态/接口 |
|---|---|---|---|
| 测量引擎 | `src/app/measure.cpp/.h` | 轮询 INA226/ADS1115、快照、自动停止判定 | `MeasurementSnapshot`、`getLatest()`、`below_cnt[4]` |
| 录制器 | `src/app/record.cpp/.h` | 环形缓冲(60)、.dat 落盘、命名/清理、崩溃恢复、.done | `DataRecorder::getInstance()`、`_states[4]` |
| 配置 | `src/app/config.cpp/.h` | NVS 存储 | `DeviceSettings::getInstance()`、`StopCond[4]` |
| 网络服务 | `src/net/server.cpp/.h` | AP/STA、NTP、HTTP REST、WS 命令队列+广播、流推送 | `PowerMeterWebServer::getInstance()`、`s_tx_mode` 状态机 |
| 日志 | `src/net/log.cpp/.h` | WebLog（LittleFS 环形，/api/logs） | `WebLog::getInstance()`、`fsReady` |
| 屏幕 | `src/app/screen.cpp/.h` | LVGL UI | `refresh_status_bar()`、通道卡片 label 指针 |
| 蜂鸣器 | `src/hal/buzzer.cpp/.h` | LEDC 蜂鸣（标志驱动） | `g_buzzer`、`beepN()`、`consumeStartBeep()` |
| 主循环 | `src/main.cpp` | 初始化、NTP、auto-resume、推送、内存监控 | `ntp_done`、`auto_resume_done` |

### 2.2 关键机制（跨任务安全）

1. **WS 命令队列**：WS 事件（AsyncTCP 任务）只写 `s_cmd_pending/s_cmd_ch/s_cmd_file/s_cmd_client/s_cmd_reqid`；main loop `processCmdQueue()` 执行（stream_start/download_start/abort）——**消除流状态竞态（panic 根因）**
2. **AsyncTCP 加固**：事件队列 `xQueueCreate(32→256)`——防 lwIP 线程 `xQueueSend(portMAX_DELAY)` 无限阻塞
3. **发送背压**：`cl->client()->space() >= 整块字节` 才发送——防部分发送积压 WS 队列（WS_MAX_QUEUED_MESSAGES=16）
4. **WiFi 省电**：`WiFi.setSleep(false)`——降低 TX 唤醒延迟/ack timeout
5. **广播暂停**：流/下载传输期间 `ws_send_busy_until=0xFFFFFFFF`（全程停广播，done/abort 恢复）

### 2.3 录制器内部

```
startChannel(ch, name)
  → _buildFilename()：名字_chN_YYYYMMDD_HHMMSS.dat（本地时间）
  → 打开文件、清缓冲、armed 复位
resumeChannel(ch, name, file)   // 崩溃恢复
  → 打开 /data/file（存在校验）、start_time 回拨（含历史时长）
  → sample_count = 文件样本数
stopChannel(ch)
  → _flushBuffer()（缓冲 60 样本写盘）
  → 文件改名 .dat → .dat.done（完成标记）
  → last_file 存纯文件名（无 /data/）
_fillBuffer() / _flushBuffer()
  → 每样本写入环形缓冲[60]；满 60 → flush 到文件（FILE_APPEND）
_enforceFileLimit()      // 开始录制时：.dat+.done > 10 → 删最旧（按文件名排序）
_ensureSpaceFor8h()      // 按采样率计算 8h 数据量，不足删最旧
findIncomplete(out[4][64])  // 扫描无 .done 的 .dat，文件名时间 48h 内（NTP 未同步时全报）
```

---

## 3. 数据设计

### 3.1 .dat 文件格式（24B/样本，packed，无头部）

```
偏移  大小  字段              类型       说明
0     4    timestamp        uint32     epoch 秒（UTC）
4     4    bus_voltage_V    float      INA226 总线电压（V）
8     4    current_A        float      电流（A，充电正/供电负）
12    4    power_W          float      功率（W）
16    4    channel_temp_C   float      通道 NTC 温度（°C，断线 -999）
20    4    ambient_temp_C   float      环境温度（°C）
```
- 文件命名：`<名字>_ch<N>_<YYYYMMDD_HHMMSS>.dat`（名字默认 "test"，本地时间）
- **完成标记**：正常停止 → 重命名 `.dat` → `.dat.done`（无 magic，无 truncate）
- 崩溃/断电 → 保持 `.dat`（auto-resume 依据）
- 读取：按 24B 步进（`o+24<=size`），尾部不足 24B 忽略
- 客户端解析（网页分页/下载）：`DataView` little-endian（getUint32/getFloat32）

### 3.2 NVS 键表（Preferences namespace: `settings`）

| 键名 | 类型 | 默认值 | 范围/约束 | 用途 |
|---|---|---|---|---|
| `dev_name` | String | `"PowerMeter"` | ≤31 字符 | 设备名（AP SSID 前缀） |
| `ap_ssid` | String | `"PowerMeter-4CH"` | ≤31 字符 | AP SSID（实际运行时 = 设备名-MAC尾缀） |
| `ap_pass` | String | `"12345678"` | ≤31 字符 | AP 密码 |
| `wifi_ssid` | String | 空 | ≤31 字符 | STA 目标 SSID |
| `wifi_pass` | String | 空 | ≤63 字符 | STA 密码 |
| `sample_ms` | UInt32 | `1000` | 100~60000 | 采样间隔（ms） |
| `temp_unit` | Char | `'C'` | `'C'`/`'F'` | 温度单位 |
| `tz_offset` | Char(int8) | `8` | -12~14 | UTC 偏移（小时） |
| `rotation` | UShort | `0` | 0/180（90/270 归一为 0） | 屏幕旋转 |
| `amb_toff` | Float | `0` | 无限制 | 环境温度校准偏移 |
| `stop_cond` | Blob | 4×StopCond 默认值 | sizeof(StopCond)×4 | 自动停止条件（见下） |
| `ch0_name`~`ch3_name` | String | — | — | **旧键（不再存储，save 时 remove 清理）** |

**StopCond 结构**（sizeof：bool+float+float+uint16+bool+bool+bool+float+float，packed 后 ~26B；blob 4 通道）：

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 该通道自动停止总开关 |
| `voltage_threshold_V` | float | 0.5 | 电压低于此值（V） |
| `current_threshold_mA` | float | 10.0 | 电流低于此值（mA） |
| `max_duration_min` | uint16 | 480 | 最长录制时长（分钟，8h） |
| `falling_edge` | bool | true | 下降沿模式（需先高于阈值再落下才触发） |
| `armed_V` | bool | false | 运行时：电压已武装（曾高于阈值） |
| `armed_mA` | bool | false | 运行时：电流已武装 |
| `peak_V` | float | 0 | 运行时：电压峰值 |
| `peak_mA` | float | 0 | 运行时：电流峰值 |

> 运行时字段（armed/peak）也随 blob 持久化（无害，开机重置）。

### 3.3 WebLog（LittleFS `/data/system.log`）

- 环形（最近 ~16KB，`WebLog::loadAll()` 全量返回）
- HTTP `/api/logs` 读取（网页/调试用）
- 记录：启动横幅、NTP、WiFi、录制、流事件
- `fsReady` 标志：LittleFS 挂载前跳过写入（防 vfs 错误）

---

## 4. 接口设计

### 4.1 WebSocket 协议（`ws://<ip>/ws`）

**设备 → 网页**（文本 JSON / 二进制帧）：

#### 4.1.1 `status`（连接时 WS_EVT_CONNECT 发送 / `get_status` 命令响应）

```json
{
  "type": "status",
  "any_recording": false,
  "recording": [
    {"active":false,"elapsed":"00:00","samples":0,"last_file":""},
    ... ×4
  ],
  "incomplete": [
    {"ch":0,"file":""},
    ... ×4
  ]
}
```
- `recording[i].last_file`：纯文件名（无 /data/，含 .done 后缀）
- `incomplete[i].file`：崩溃残留候选（无 .done 的 .dat）

#### 4.1.2 `measurement`（广播，采样间隔频率，默认 1s）

```json
{
  "type": "measurement",
  "timestamp": 1786089191,
  "env": {"ambient_temp_C": 27.65},
  "channels": [
    {"bus_voltage_V":7.9,"current_mA":0,"power_mW":0,
     "channel_temp_C":27.19,"connected":true},
    ... ×4
  ],
  "recording": [{"active":false,"elapsed":"00:00","samples":0,"last_file":""}, ...],
  "wifi_connected": true, "wifi_ssid": "QWERT", "wifi_ip": "192.168.100.243",
  "wifi_rssi": -59, "scope_ch": -1
}
```
- `timestamp`：epoch 秒（UTC）
- `current_mA`：mA（充电正/供电负）；`power_mW`：mW
- `channel_temp_C`：断线 = -999（网页显示 `---`）
- `scope_ch`：Scope 模式高亮通道（-1 无）

#### 4.1.3 `ack`（命令响应）

```json
{"type":"ack","cmd":"record_all","ok":true,"req_id":130,"data":[...],"total":6845}
```
- `req_id`：回显请求 id（网页 wsReq 匹配）
- `data`：命令相关数据
- 超时：网页 15s（分页）、5s（其他）

#### 4.1.4 命令表（网页 → 设备，`{type:"cmd", cmd, req_id, ...}`）

| 命令 | 参数 | 响应 | 说明 |
|---|---|---|---|
| `get_settings` | - | 全量设置（同 GET /api/settings） | - |
| `settings` | 任意设置字段（见 HTTP POST） | ok | 改设置（含 stop_ch/stop_en/stop_v/stop_mA/stop_min/stop_fall） |
| `get_status` | - | status 负载 | 连接后恢复/崩溃提示 |
| `record_start` | ch, name?, resume_file? | ok | resume_file 非空 = 续录（崩溃恢复） |
| `record_stop` | ch | ok | - |
| `rename` | ch, name | ok | 录制中改名（生效当前） |
| `files` | - | data[]={name,size}（含 .dat/.done） | - |
| `delete` | file | ok | 校验无路径分隔符 |
| `storage` | - | total_kb/used_kb/free_kb | - |
| `record_all` | ch, offset, limit(≤500), file? | data[]={t,V,A,W,C} + total | 分页历史（file=指定停止文件） |
| `download_start` | file | ok → 二进制帧流 → `dl_done`/`dl_fail` | 文件下载 |
| `restart` | - | ok（设备重启） | - |
| `buzzer` | - | ok | 蜂鸣测试 |

**二进制帧**（download_start 后）：原始文件字节（2048B/块），结尾 JSON `{"type":"dl_done"}` 或 `{"type":"dl_fail","error":...}`

### 4.2 HTTP REST（Agent/AI 用，网页不再使用）

| 方法 | 路径 | 参数 | 响应 |
|---|---|---|---|
| GET | `/ping` | - | `pong` |
| GET | `/api/status` | - | 同 WS status + env + channels + wifi_rssi |
| GET | `/api/settings` | - | 全量设置 JSON |
| POST | `/api/settings` | 表单：dev_name/ap_password/wifi_ssid/wifi_password/sample_interval_ms/temp_unit/tz_offset/rotation/amb_temp_offset/stop_ch+stop_en+stop_v+stop_mA+stop_min+stop_fall | `{ok:true}` |
| GET | `/api/files` | - | `[{name,size},...]` |
| GET | `/api/storage` | - | `{total_kb,used_kb,free_kb}` |
| GET | `/api/logs` | - | WebLog 文本 |
| GET | `/api/channel/<0-3>/record/all` | offset, limit(≤500), file? | 样本数组（同 record_all） |
| POST | `/api/delete` | file | `{ok}` |
| POST | `/api/restart` | - | 重启 |
| POST | `/api/buzzer` | - | 蜂鸣 |

**设置字段类型**：sample_interval_ms=uint、temp_unit='C'/'F'、tz_offset=int、rotation=0/180、amb_temp_offset=float、stop_mA=mA 整数、stop_min=分钟

---

## 5. 关键流程

### 5.1 网页历史数据获取（分页）

```
刷新 → WS 连接 → get_status（恢复录制状态）
→ loadBufferData(ch, offset=0, file=停止文件?)
→ 循环：
    record_all{ch, offset, limit:200, file?}（wsReq 15s 超时）
    失败重试 3 次（间隔 2s）
    累加 data[]，进度条显示 "已获取/总数"
    ack.total 记录总数
  → data.length < 200 结束
→ 一次性 setOption 画图（X=真实时间戳，Y 最小范围）
→ 填详细数据表 dataStore
→ 日志对比：[HIST] ch0 got N / total M（MISSING 提示）
```

### 5.2 崩溃恢复（auto-resume）

```
开机 → NTP 同步成功（或 90s 兜底）
→ findIncomplete()：扫 /data
   条件：*.dat（无 .done）&& 文件名时间戳>2020
   && (NTP 未同步 || now - 文件时间 < 48h)
   （每通道取最新 1 个）
→ autoResumeInterrupted()：逐个 resumeChannel(ch, 名字前缀, file)
   → start_time 回拨（last_ts-first_ts 或 样本数×间隔）
   → 继续录制（屏幕/网页显示录制中）
```

### 5.3 自动停止（10 样本去抖）

```
每样本（仅录制中）：
  电压 < 阈值（0.5V）→ V 条件满足（下降沿：先武装才计数）
  电流 < 阈值（10mA）→ mA 条件满足
  时长 > 最大（480min）→ 时长条件满足
  enabled 条件全满足 → below_cnt++；否则 below_cnt=0
  below_cnt >= 10 → stopChannel(ch)（蜂鸣）
```

### 5.4 更新流程（桌面应用）

```
检查更新 → GET GitHub Releases API → verCmp 版本对比
→ 有新版：按钮变"下载更新" → 后台下载 zip（进度）
→ 下载完成：按钮变"更新" → 用户点击
→ 解压 → updater.bat 替换 app 文件 → 重启
→ 首次启动：显示更新成功 + changelog
```

---

## 6. 网页结构（web/index.html 单文件 ~57KB）

| 数据流 | 函数 | 说明 |
|---|---|---|
| WS 接收 | `ws.onmessage` | measurement→processData；ack→wsReq 匹配；二进制→下载收集 |
| 数值更新 | `up()` | 4 通道 V/A/W/C + 环境温度 + 录制计时 |
| 实时曲线 | `uc()` | IDLE/录制都画（recState!==2），滑动窗口 3600 点 |
| 历史加载 | `loadBufferData()` | 分页 record_all + 进度 + 一次画 |
| 详细表 | `updateTables()` | dataStore 渲染 |
| 状态机 | `recState[i]` | 0 IDLE / 1 录制 / 2 停止 |
| 命令 | `wsSend/wsReq` | 发送 / Promise 化请求（req_id 匹配） |
| 崩溃提示 | `handleStatusMsg()` | incomplete → toast 提示已自动续录 |

全局状态：`lastData`、`charts[4]`、`dataStore[4]`、`recState[4]`、`maxTs[4]`、`absMode[4]`、`firstTs[4]`、`tempUnit`

图表（ECharts 5.5 本地）：
- 4 通道 × 4 系列（V/A/W/C），yAxisIndex 0/1/1/2
- X 轴 value（epoch 秒），formatter 时间；数据 <1e9 → 相对 mm:ss
- Y 轴最小范围：V/A/W ≥0.5、C ≥1（噪声不放大）
- 绝对值模式：0 底 + max≥0.5；带符号：对称轴
- markLine y=0 虚线（0 参考）

## 7. 桌面应用（app/）

| 文件 | 职责 |
|---|---|
| main.js | 窗口（无边框 1200×850）、IPC（min/max/close/devtools/save-log/update*）、更新下载/解压/替换 |
| preload.js | contextBridge 暴露：windowControl/更新/日志 |
| dashboard/ | 网页副本（发版时同步，file:// 加载） |
| updater.bat | 更新替换脚本（等待退出→替换→重启） |

- webSecurity 开启（WS 不受 CORS 限制）+ CSP（unsafe-eval 因 ECharts）
- 更新：GitHub Releases API + zip 后台下载（进度）→ 本地暂存 → 应用时解压替换

## 8. 部署与发布

- `scripts/release.ps1 <version>`：
  1. 更新 web/index.html 版本号
  2. 同步 web → app/dashboard
  3. git commit + tag v<version>
  4. npm 打包（electron-builder --dir，去签名）→ zip（无 win-unpacked 层级）
  5. gh release create + 上传 zip + firmware.bin（build 目录）
- 检查更新：GitHub Releases API（tag 名版本对比）

---

## 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.1 | 2026-08-07 | 细化：NVS 键表（类型/默认/范围）、StopCond 字段、.dat 字节布局、WS 命令参数表、HTTP 参数、模块内部、流程步骤 |
| v1.0 | 2026-08-07 | 初始设计文档 |
