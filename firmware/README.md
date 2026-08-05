# 4-Channel Power Meter — 四通道功率计固件

> **产品名称**: 4CH Power Meter | **固件版本**: v0.2.4  
> **硬件平台**: ESP32-S3 / Lilygo T-Display-S3  
> **开发框架**: Arduino (PlatformIO)

---

## 1. 项目概述

本固件是 **O3601V10 四通道功率计主板** 的嵌入式软件，实现 4 路独立电源通道的**电压、电流、功率、温度**同步测量。测量数据通过以下方式呈现：

- **T-Display-S3 屏幕**：LVGL 仪表盘，4 通道卡片 + 环境温度 + 录制状态
- **Web 仪表盘**：WiFi AP 模式，浏览器直连 `http://192.168.4.1`
- **CSV 录制**：4 通道独立启停，数据存入 LittleFS，支持 HTTP 下载

### 硬件架构（4 块 PCB）

| 子板 | 说明 |
|------|------|
| O3601V10 4通道功率计主板 | ESP32-S3 + ADS1115 ADC + 蜂鸣器 + 环境 NTC |
| O3602V10 INA226 测量通道 | INA226 电流传感器 + AON7544 MOSFET + 5mΩ/2512 分流电阻 |
| O3603V10 4线测量接口（输入） | 7.62mm 端子，Kelvin 4线连接 |
| O3604V10 4线测量接口（输出） | 7.62mm 端子，Kelvin 4线连接 |

---

## 2. 核心功能

### 2.1 测量能力

| 参数 | 规格 |
|------|------|
| 通道数 | 4 |
| 电压测量 | 0~36V (INA226 总线电压) |
| 电流测量 | 0~16.4A (5mΩ 分流电阻) |
| 功率测量 | 自动计算 V × I |
| 温度测量 | B3950 10K NTC，每通道独立 + 1 路环境温度 |
| 非录制刷新率 | 1Hz（显示用） |
| 录制采样率 | 可配置 100ms ~ 60s（默认 1s） |

### 2.2 传感器拓扑

```
ESP32-S3 I2C (GPIO43=SDA, GPIO44=SCL, 400kHz)
├── ADS1115 (0x48) —— 4路ADC采样
│   ├── CH0: 通道1 NTC
│   ├── CH1: 通道2 NTC
│   ├── CH2: 通道3 NTC
│   └── CH3: 通道4 NTC
├── INA226 CH1 (0x40) — 通道1 电压/电流/功率
├── INA226 CH2 (0x41) — 通道2 电压/电流/功率
├── INA226 CH3 (0x44) — 通道3 电压/电流/功率
├── INA226 CH4 (0x45) — 通道4 电压/电流/功率
└── 环境 NTC (GPIO1, ESP32-S3 内置 ADC)
```

### 2.3 WiFi & Web 服务

- **默认 AP 模式**: SSID=`4CH Power Meter-XXXXXX`, 密码=`12345678`, IP=`192.168.4.1`
- **可选 STA 模式**: 连接外部路由器，同时保持 AP 在线（WIFI_AP_STA）
- **内嵌仪表盘**: 实时 4 通道数值 + 录制控制，WebSocket 500ms 推送
- **REST API**: 完整的设置/控制/数据管理接口

### 2.4 数据录制

- **4 通道独立启停**: 各通道可命名、可独立开始/停止录制
- **存储**: LittleFS，每个通道一个 CSV 文件 `/data/CH{N}_{name}.csv`
- **CSV 格式**: `timestamp_ms,Voltage_V,Current_mA,Power_mW,Temp_C,Ambient_TempC`
- **下载**: 通过 Web API 直接下载 CSV
- **文件管理**: 列出/删除录制文件

### 2.5 报警系统

- 每通道支持 2 个独立报警条件
- 可选条件: 电压/电流/功率/温度 高于/低于 阈值
- 持续时间消抖: 条件需持续满足才触发
- 触发动作: 蜂鸣器警告 + Web 通知

---

## 3. 目录结构

```
4-ch-power-meter-firmware/
├── platformio.ini              # PlatformIO 项目配置
├── src/
│   ├── main.cpp                # 入口：setup() / loop()
│   ├── version.h               # 产品名和版本号
│   ├── drivers/                # 硬件驱动层
│   │   ├── pin_config.h        # 引脚定义 + I2C 地址 + 校准参数
│   │   ├── ina226.h/cpp        # INA226 电流传感器驱动
│   │   ├── ads1115.h/cpp       # ADS1115 16位ADC驱动
│   │   ├── ntc_b3950.h/cpp     # B3950 NTC 温度转换
│   │   ├── buzzer.h/cpp        # 无源蜂鸣器 (非阻塞 PWM)
│   │   ├── button.h/cpp        # 按键驱动
│   │   └── channel_control.h/cpp  # 通道 EN0 开关控制
│   ├── apps/                   # 应用层
│   │   ├── apps.h              # App 框架定义 + 注册表
│   │   ├── measurement_data.h  # 测量数据结构定义
│   │   ├── measurement_engine.h/cpp  # 核心测量引擎（协调传感器）
│   │   ├── data_recorder.h/cpp # LittleFS CSV 录制
│   │   ├── settings.h/cpp      # NVS 持久化设置
│   │   ├── power_meter_app.h/cpp    # 主屏幕 LVGL UI
│   │   ├── launcher.cpp        # 启动页
│   │   ├── about.cpp           # 关于页
│   │   └── wifi_config.cpp     # WiFi 信息页
│   ├── framework/              # UI 框架
│   │   ├── framework.h/cpp     # 框架入口 (init/loop/app 管理)
│   │   ├── ui.h/cpp            # LVGL UI 组件库
│   │   ├── lvgl_port.h/cpp     # LVGL 显示驱动适配
│   │   └── fonts/              # 中文字体
│   └── www/                    # Web 服务
│       ├── web_server.h/cpp    # WiFi AP + REST API + WebSocket
│       ├── dashboard_html.h    # 内嵌 HTML 仪表盘
│       └── weblog.h/cpp        # 运行日志环形缓冲
└── lib/
    └── lvgl/                   # LVGL v9 图形库
```

---

## 4. 构建与烧录

### 4.1 环境要求

- [PlatformIO IDE](https://platformio.org/) (VS Code 插件)
- ESP32-S3 工具链 (PlatformIO 自动下载)
- USB-C 数据线连接 T-Display-S3

### 4.2 构建

```bash
cd 4-ch-power-meter-firmware
pio run
```

### 4.3 烧录

```bash
pio run -t upload
```

> 默认串口 `COM16`，可在 `platformio.ini` 中修改 `upload_port`

### 4.4 串口监控

```bash
pio device monitor -b 115200
```

### 4.5 分区表

固件使用 `default_16MB.csv` 分区表（16MB Flash），为 LittleFS 数据存储留出充足空间。

---

## 5. 使用说明

### 5.1 开机流程

1. 上电 → 蜂鸣器自检 → I2C 扫描 INA226/ADS1115 → 挂载 LittleFS → 初始化 LVGL 屏幕 → 启动 WiFi AP
2. 屏幕显示 4 通道仪表盘
3. 蜂鸣器播放 "do-re-mi" 启动提示音

### 5.2 屏幕布局

```
┌──────────────────────────────────┐
│ 12:30     ● IDLE         25.3°C │  顶栏: 时间 录制状态 环境温度
├──────┬──────┬──────┬──────────────┤
│  CH1 │  CH2 │  CH3 │  CH4        │
│5.00V │3.30V │12.0V │1.80V       │  每通道: V / mA / W / °C
│120mA │50mA  │2.3A  │800mA       │
│0.60W │0.17W │27.6W │1.44W       │  橙色条=已连接, 灰色条=离线
│35.2° │32.1° │45.8° │30.0°       │
├──────┴──────┴──────┴──────────────┤
│ PowerMeter-4CH-XXXX  192.168.4.1  │  底栏: SSID / IP
└──────────────────────────────────┘
```

### 5.3 按键操作

| 按键 | 短按 | 长按 |
|------|------|------|
| Button A (BOOT) | 上/下一个通道 | 返回 |
| Button B (USER) | 下/上一个通道 | 确认 |

### 5.4 Web 仪表盘

1. 手机/电脑连接 WiFi: `4CH Power Meter-XXXXXX`（密码 `12345678`）
2. 浏览器打开 `http://192.168.4.1`
3. 实时查看数据、启停录制、下载 CSV、修改设置

---

## 6. REST API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/` | 仪表盘 HTML 页面 |
| `GET` | `/ping` | 健康检查 → `pong` |
| `GET` | `/api/status` | 当前测量数据 + 录制状态 |
| `GET` | `/api/settings` | 获取全部设备设置 |
| `POST` | `/api/settings` | 更新设置 (form-encoded) |
| `POST` | `/api/channel/<0-3>/record/start` | 开始录制某通道 `?name=xxx` |
| `POST` | `/api/channel/<0-3>/record/stop` | 停止录制某通道 |
| `POST` | `/api/channel/<0-3>/name` | 重命名通道 `?name=xxx` |
| `POST` | `/api/buzzer/test` | 测试蜂鸣器 |
| `GET` | `/api/files` | 列出录制文件 |
| `GET` | `/api/download?file=xxx.csv` | 下载 CSV |
| `POST` | `/api/delete?file=xxx.csv` | 删除文件 |
| `POST` | `/api/wifi` | 配置 WiFi STA `?ssid=x&password=x` |
| `POST` | `/api/restart` | 设备重启 |
| `GET` | `/api/i2c/scan` | I2C 传感器检测状态 |
| `GET` | `/api/logs` | 设备运行日志 |
| `WS` | `/ws` | WebSocket 实时数据推送 |

### WebSocket 消息格式

**measurement** (500ms 推送):
```json
{
  "type": "measurement",
  "data": {
    "timestamp": 12345,
    "channels": [
      {"bus_voltage_V": 5.0, "shunt_voltage_mV": 0.06, "current_mA": 120.0, "power_mW": 600.0, "channel_temp_C": 35.2, "connected": true, "name": "CH1"},
      "..."
    ],
    "env": {"ambient_temp_C": 25.3},
    "recording": [{"active": true, "elapsed": "02:30", "samples": 150, "name": "test"}],
    "any_recording": true
  }
}
```

**alarm** (触发时):
```json
{"type": "alarm", "channel": 0, "message": "CH1 alarm triggered!"}
```

---

## 7. 配置参数

| 参数 | 默认值 | 说明 | 持久化 |
|------|--------|------|--------|
| `device_name` | `4CH Power Meter` | 设备名（AP SSID 前缀） | NVS |
| `ap_password` | `12345678` | AP 密码（需 ≥8 字符） | NVS |
| `wifi_ssid` | (空) | STA 路由器 SSID | NVS |
| `wifi_password` | (空) | STA 路由器密码 | NVS |
| `sample_interval_ms` | `1000` | 录制采样间隔 (100-60000) | NVS |
| `temp_unit` | `C` | 温度单位 (C/F) | NVS |
| `channel_names[0-3]` | `CH1-CH4` | 通道自定义名称 | NVS |

---

## 8. 关键依赖

| 库 | 版本 | 用途 |
|----|------|------|
| `bblanchon/ArduinoJson` | ^7.2.2 | JSON 序列化 |
| `me-no-dev/AsyncTCP` | ^3.2.5 | 异步 TCP 库 |
| `me-no-dev/ESPAsyncWebServer` | ^3.2.3 | 异步 Web 服务器 |
| LVGL | v9 (本地) | 图形界面 |
| Arduino Framework | ESP32-S3 | 基础框架 |

---

## 9. 引脚定义

参照 `src/drivers/pin_config.h`。关键引脚：

| 功能 | GPIO | 备注 |
|------|------|------|
| I2C SDA | 43 | |
| I2C SCL | 44 | |
| 蜂鸣器 | 2 | LEDC PWM |
| 环境 NTC | 1 | ESP32 ADC |
| 通道A EN0 | 17 | 主开关 |
| 通道B EN0 | 21 | 主开关 |
| 通道C EN0 | 3 | 主开关 |
| 通道D EN0 | 12 | 主开关 |
| Button A | 0 | BOOT 按钮 |
| Button B | 14 | USER 按钮 |

---

## 10. 注意事项

1. **分流电阻校准**: `INA226_SHUNT_MOHM` 设为 `5.0`（5mΩ 实测值），更换分流电阻须修改此值
2. **AP 密码长度**: ESP32 softAP 要求密码长度 ≥8 或为空（开放网络），固件会自动检查
3. **STA 重试策略**: 前 5 次每 5s 重试，之后每 30s 重试，共 10 次后放弃
4. **录制时通道 EN0**: 启动录制自动拉高 EN0，停止后自动拉低
5. **CSV flush**: 每 10 条样本 flush 一次，防止数据丢失
