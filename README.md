# SunFounder 功率计 Dashboard

4通道功率计的上位机：网页仪表盘 + Windows 桌面应用。

```
dashboard/
├── web/          # 网页仪表盘（GitHub Pages 源）
├── app/          # Electron 桌面应用
├── docs/         # 文档
└── dist/         # 打包产物（zip）
```

## 发版流程

1. 修改 `web/` 网页代码
2. `web/version.json` + `web/index.html` 中 `DASH_VER` 同步升版本号
3. 提交并 push（GitHub Pages 自动部署 `web/`）
4. 同步网页到 APP：`scripts/sync.ps1`
5. 打包：`cd app && npm run build` → `dist/win-unpacked/` 压缩为 zip
6. 发布 GitHub Release 附 zip

## 更新机制

- **网页**：打开时自动检查 `web/version.json`，发现新版本提示更新
- **APP**：文件夹版，替换 `resources/app/dashboard/` 即更新（免重装）

## 设备

- ESP32-S3 (T-Display-S3) + INA226×4 + ADS1115 + NTC
- 固件：`power-meter-firmware` 仓库
- REST API：见 `web/docs.html`
