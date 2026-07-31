# Power Meter Dashboard v1.1

SunFounder 4-Channel Power Meter 仪表板。

## 使用

用浏览器打开：`https://sunfounder.github.io/power-meter-dashboard/`

输入设备 IP 地址连接。

## 功能

- 4 通道实时数据（V/A/W/°C）+ ECharts 图表
- 独立通道录制（REC/STOP/Reset），二进制 .dat 存储
- Flash 存储管理（文件列表/下载/删除/空间进度条）
- 自动停止条件（电压/电流/时长/下降沿）
- 温度单位切换、时区设置、密码显示切换
- 全量历史数据加载（刷新后图表恢复）
- 纯 HTTP 轮询，无需 WebSocket

## API 文档

点击右上角 ⓘ 按钮，或直接打开 `docs.html`。

## 设备依赖

- [power-meter-firmware](https://github.com/sunfounder/power-meter-firmware) v0.3.0+
