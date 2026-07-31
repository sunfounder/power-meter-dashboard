# 4CH Power Meter — 需求文档

> 最后更新: 2024-07-27

## 1. Web 仪表盘 (Dashboard)

### 1.1 设置弹窗 (Settings Modal)
- 入口: 页面右上角 ⚙ 齿轮图标
- 包含:
  - 设备名称 (Device Name)
  - AP SSID / 密码 (修改后 AP 自动重启)
  - WiFi STA 配置 (连接已有网络)
  - 蜂鸣器测试按钮
  - 固件版本号 (只读)
  - 采样间隔设置
- 保存后通过 NVS 持久化，断电不丢失
- **可扩展**: 以后可以添加更多设置项

### 1.2 通道布局 (4-Row Layout)
- 4 行，每行一个通道
- 左侧 (info 区):
  - 通道名称 + ✎ 改名按钮
  - V / I / P / T 实时数据
  - EN1 / EN2 状态徽章 (点击切换)
- 右侧 (action 区):
  - 测试名称
  - 计时器 MM:SS
  - 采样计数
  - ▶ REC / ⏹ STOP 按钮

### 1.3 通道改名
- 每通道旁有 ✎ rename 按钮
- 点击弹出 prompt，输入新名字
- 保存到 NVS，断电不丢失

### 1.4 文件管理
- 底部列出所有 CSV 文件
- 下载 ⬇ / 删除 ✕ 按钮
- 每 5 秒自动刷新列表

---

## 2. 数据录制

### 2.1 每通道独立录制
- 4 个通道**各自独立**的开始/停止
- 可同时录制 1~4 个通道
- 每个通道独立的计时器和采样计数

### 2.2 文件命名
- 格式: `CH{N}_{测试名}.csv`
- 例: `CH1_负载测试.csv`, `CH2_负载测试.csv`

### 2.3 CSV 格式 (每通道独立文件)
```
timestamp_ms,Voltage_V,Current_mA,Power_mW,Temp_C,EN1,EN2,Ambient_TempC
```

### 2.4 录制控制 API
- `POST /api/channel/{0-3}/record/start` — body: `name=<test_name>`
- `POST /api/channel/{0-3}/record/stop`
- 启动时自动开启对应通道的 EN0

---

## 3. 设置存储 (NVS)

### 3.1 存储项
| Key | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| dev_name | string | "4CH Power Meter" | 设备名称 |
| ch0_name ~ ch3_name | string | "CH1"~"CH4" | 通道名 |
| ap_ssid | string | "PowerMeter-4CH" | AP SSID |
| ap_pass | string | "12345678" | AP 密码 (≥8字符或空) |
| wifi_ssid | string | "" | STA WiFI SSID |
| wifi_pass | string | "" | STA WiFI 密码 |
| sample_ms | uint32 | 1000 | 采样间隔(ms) |

### 3.2 密码校验
- AP 密码必须 ≥ 8 个字符或为空(开放网络)
- 保存时如果密码过短，返回 400 错误
- 启动时如果密码过短，自动 fallback 到默认值

---

## 4. API 端点

### 4.1 状态
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/status` | 4通道实时数据 + 录制状态 |
| WS | `/ws` | WebSocket 实时推送 (10Hz) |

### 4.2 设置
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/settings` | 读取所有设置 |
| POST | `/api/settings` | 保存设置 (form/JSON) |

### 4.3 通道控制
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/channel/{0-3}/record/start` | 开始录制 |
| POST | `/api/channel/{0-3}/record/stop` | 停止录制 |
| POST | `/api/channel/{0-3}/name` | 重命名通道 |
| POST | `/api/channel/{0-3}/en1` | 切换 EN1 |
| POST | `/api/channel/{0-3}/en2` | 切换 EN2 |

### 4.4 其他
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/buzzer/test` | 蜂鸣器测试 (响3声) |
| GET | `/api/files` | 列出 CSV 文件 |
| GET | `/api/download?file=xxx` | 下载 CSV |
| POST | `/api/delete?file=xxx` | 删除 CSV |
| POST | `/api/wifi` | 连接 STA WiFi |

---

## 5. 待实现 (Future)

- [ ] 设备屏幕 LVGL UI 改版 (4行布局)
- [ ] 多语言支持
- [ ] HTTPS / 身份认证
- [ ] 远程固件升级 (OTA)
- [ ] SD 卡支持 (大容量存储)
- [ ] 告警配置界面 (阈值设置)

## 6. 已知问题 & 修复记录

### GPIO10 冲突 (v0.2.0)
- **问题**: `PIN_BUZZER` 和 `PIN_CHA_EN0` 都配置为 GPIO10
- **现象**: 蜂鸣器发热，通道A EN0 和蜂鸣器互相干扰
- **根因**: 原理图批量复制未改引脚号，两个 label 都标了 GPIO10
- **临时修复**: CHA_EN0 暂改 GPIO1，**需飞线**

### 原理图引脚标注错误 (待修复)
- 多个通道 EN 引脚对应关系原理图上标注不清
- 部分引到了空 IO / 未连接的引脚
- **计划**: 下一版 PCB 彻底修复

---

## 7. 硬件 TODO — V1 手动修改 (不重新打样)

> 当前版本 PCB 有引脚冲突和标注错误。在下一版 PCB 之前，
> 通过割线 + 飞线手动修复。以下每个步骤完成后打 ✅。

### 7.1 蜂鸣器 / CHA_EN0 冲突 ⚡ 优先

```
PCB 现状: 蜂鸣器 AO3400A Gate ←→ CHA_EN0 ←→ GPIO10（短路！）
```

| 步骤 | 操作 | 状态 |
|------|------|------|
| 1 | 割断 CHA_EN0 到 GPIO10 的走线 | [ ] |
| 2 | 飞线: CHA_EN0 焊盘 → T-Display-S3 排针 **GPIO1** | [ ] |
| 3 | 蜂鸣器保留在 GPIO10（不动） | [ ] |
| 4 | 确认 pin_config.h: `PIN_CHA_EN0 = 1`, `PIN_BUZZER = 10` | [x] |

### 7.2 通道 EN 引脚逐通道验证

用万用表蜂鸣档，逐个确认每个通道的 EN0/EN1/EN2 实际连到 T-Display-S3 的哪个排针。

| 通道 | 信号 | 期望 GPIO | 实测 GPIO | pin_config.h |
|------|------|-----------|-----------|-------------|
| CHA | EN0 | GPIO1 | ___ | `PIN_CHA_EN0` |
| CHA | EN1 | GPIO11 | ___ | `PIN_CHA_EN1` |
| CHA | EN2 | GPIO12 | ___ | `PIN_CHA_EN2` |
| CHB | EN0 | GPIO13 | ___ | `PIN_CHB_EN0` |
| CHB | EN1 | GPIO16 | ___ | `PIN_CHB_EN1` |
| CHB | EN2 | GPIO17 | ___ | `PIN_CHB_EN2` |
| CHC | EN0 | GPIO18 | ___ | `PIN_CHC_EN0` |
| CHC | EN1 | GPIO21 | ___ | `PIN_CHC_EN1` |
| CHC | EN2 | GPIO2 | ___ | `PIN_CHC_EN2` |
| CHD | EN0 | GPIO3 | ___ | `PIN_CHD_EN0` |
| CHD | EN1 | GPIO42? | ___ | `PIN_CHD_EN1` |
| CHD | EN2 | GPIO41? | ___ | `PIN_CHD_EN2` |

> ⚠️ CHD EN1/EN2 标的是 GPIO42/41，但这两个和 LCD D2/D3 冲突。
> 如果实际 PCB 上确认冲突，需要飞线到其他空闲 IO（候选: GPIO1 如果未被 CHA_EN0 占用）。

### 7.3 飞线操作指南

**工具**: 手术刀/刻刀、细漆包线(0.1mm)、烙铁、万用表

```
割线:
  1. 用刀片在走线上划两刀（间距 >1mm）
  2. 挑掉中间铜皮
  3. 万用表确认断路

飞线:
  1. 漆包线两头剥漆/上锡
  2. 一端焊目标焊盘，另一端焊排针
  3. 用胶带/热熔胶固定线体
  4. 万用表确认通断
```

### 7.4 下一版 PCB 改进 (V2)

#### IO 精简方案: 每通道 3 IO → 2 IO

```
V1: EN0 + EN1 + EN2 = 3 根 IO/通道 × 4 通道 = 12 根
V2: EN  + SEL         = 2 根 IO/通道 × 4 通道 = 8 根 (省 4 根!)
```

**电路** (每通道):
```
ESP32 IO_EN ──┬── R1(10K) ── Gate Q1 (AO3400A N-MOS) → EN1 输出
               │
ESP32 IO_SEL ─┼── R2(10K) ── Gate Q2 (AO3400A N-MOS) → EN2 输出
               │
               └── 74HC14(反相器) ── (内部驱动 Q1/Q2 的其中一个)
```

- IO_EN=LOW → P-MOSFET 导通，通道上电
- IO_SEL=HIGH → EN1 导通 / EN2 截止
- IO_SEL=LOW → EN1 截止 / EN2 导通
- 4 通道共用 1 片 74HC14 (TSSOP-14, 6 路反相器用 4 路)
- BOM 增加: 1×74HC14 + 每通道 2×AO3400A + 4×10K 电阻

**空闲 IO 统计 (V2)**:
| GPIO | 功能 |
|------|------|
| 1 | CHA_EN |
| 2 | CHA_SEL |
| 3 | CHB_EN |
| 10 | Buzzer |
| 11 | CHB_SEL |
| 12 | CHC_EN |
| 13 | CHC_SEL |
| 14 | Button B |
| 16 | CHD_EN |
| 17 | CHD_SEL |
| 18 | (空闲 / 其他用途) |
| 21 | (空闲) |
| 42,41 | **禁止使用** (LCD 总线冲突) |
