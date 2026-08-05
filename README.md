# SunFounder 功率计 Dashboard

4通道功率计上位机：网页仪表盘 + Windows 桌面应用 + 设备固件。

```
dashboard/
├── web/          # 网页仪表盘（GitHub Pages 源）
├── app/          # Electron 桌面应用
├── firmware/     # ESP32-S3 设备固件 (PlatformIO)
├── scripts/      # 发版脚本
└── dist/         # 打包产物（zip）
```

## 发版流程

1. 修改 `web/` 网页代码
2. `web/version.json` + `web/index.html` 中 `DASH_VER` 同步升版本号
3. 提交并 push（GitHub Pages 自动部署 `web/`）
4. 同步网页到 APP：`scripts/release.ps1` 一键完成（同步→升版本→打包→发 Release）
5. 固件编译：`cd firmware && pio run`，产物 `firmware.bin` 随 Release 上传

## 更新机制

- **网页**：打开时自动检查 GitHub Release，发现新版本后台下载
- **APP**：文件夹版，更新包下载后一键替换重启（`resources/app/dashboard/`）
- **固件**：需 USB 烧录（`pio run -t upload`）

## 设备

- ESP32-S3 (T-Display-S3) + INA226×4 + ADS1115 + NTC
- 存储：LittleFS（.dat 二进制 24B/样本，最多 10 文件 + 8h 空间保障）
- 自动停止：每通道独立条件（电压/电流/时长/下降沿 + 10 样本去抖）
- REST API：见 `web/docs.html` 或网页右上角 ⓘ
