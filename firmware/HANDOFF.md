# HANDOFF.md — 4通道功率计固件交接文档

> 最后更新: 2025-07-29 | 固件版本: v0.2.4

---

## 快速开始

```bash
cd "F:\1.PCB开发\_测试工具\4通道 功率计\4-ch-power-meter-firmware"
pio run -t upload    # 编译+烧录 (COM16)
pio device monitor   # 串口监控 (115200)
```

---

## 当前状态

### ✅ 已完成

| 模块 | 状态 |
|------|------|
| 分流电阻值 | 5mΩ (匹配 BOM R7=5mR) |
| I2C 读取 | 单循环合并 INA226+ADS1115, 热插拔已移除 |
| 电流单位 | A (3位小数), 屏幕+网页同步 |
| 屏幕 UI | flex 连体卡片, 彩色单位 badge, 断开灰色 |
| 网页 UI | 本地 dashboard.html, 弹窗连接, 设置窗口, 文件管理 |
| 固件模式 | 纯 API (Flash 28%), CORS + OPTIONS |
| 安全 | 密码掩码, 路径遍历过滤 |
| 精度 | V:1.25mV, I:0.5mA, 通道温度:±1°C |

### 🔴 待修复

1. **REC 录制崩溃** — 按 REC 后 ESP32 重启
   - **已定位**: ESP32 LittleFS 的 `printf`/`println` 在 `open` 后立即写文件会崩
   - **最新尝试**: `snprintf`→`print(line)` 替代 `printf` (dr8.cpp)
   - **待验证**: 烧录后按 REC, 看串口是否有 `handler done` + `OK`
   - **备用方案**: 若 print 也崩, 改用非阻塞延迟写入或换 SPIFFS

2. **网页 0 值显示** — 电压 0V 时显示 `---` 而非 `0.000`
   - dashboard.html 已改为 `raw==null||NaN→0` 直接显示

3. **开关无法关闭** — V- 和 IN- 跳线帽形成旁路
   - 详见 `TODO.md`

---

## 关键文件

| 文件 | 说明 |
|------|------|
| `src/drivers/pin_config.h` | `INA226_SHUNT_MOHM=5.0`, I2C地址, GPIO |
| `src/apps/measurement_engine.cpp` | 核心测量, I2C扫描, 采样循环 |
| `src/apps/data_recorder.cpp` | LittleFS 录制 (~110行, 含崩溃修复) |
| `src/apps/power_meter_app.cpp` | LVGL 屏幕 UI (~290行) |
| `src/www/web_server.cpp` | HTTP API+路由+CORS (~730行) |
| `src/drivers/ina226.cpp` | INA226 驱动, debug dbg_cnt |
| `dashboard.html` | 网页仪表盘 (项目根目录, 电脑本地打开) |

---

## 网页使用

1. 电脑连设备 WiFi: `PowerMeter-XXXX` (密码 `12345678`)
2. 浏览器打开 `dashboard.html`
3. 弹窗输入 IP: `192.168.4.1` (AP模式) 或 `192.168.18.158` (STA模式)
4. 下次自动连接 (IP存 localStorage, 支持 `?ip=x.x.x.x` URL参数)

---

## REST API

Base URL: `http://<device-ip>`

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /ping | 健康检查 |
| GET | /api/status | 4通道数据+录制状态 |
| POST | /api/channel/0-3/record/start | 开始录制 |
| POST | /api/channel/0-3/record/stop | 停止录制 |
| GET | /api/files | 列出文件 |
| GET | /api/download?file=xxx | 下载CSV |
| POST | /api/delete?file=xxx | 删除文件 |
| GET/POST | /api/settings | 读写设置 |

---

## 硬件问题

- **分流电阻**: CH1/2/4 未接 INA226 (只有 CH3 连接)
- **漏电路径**: V- 和 IN- 跳线帽导致 EN1 关断无效
- **时间**: 连 STA WiFi 后 NTP 自动同步

---

## 编译信息

- Flash: ~33% (2.2MB/6.5MB)
- RAM: ~36% (120KB/328KB)
- Platform: espressif32@6.0.1 / T-Display-S3 / Arduino
