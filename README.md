# Power Meter Dashboard

Single-page dashboard for the 4-Channel Power Meter device.

## Files

| File | Purpose |
|------|---------|
| `index.html` | Main dashboard — connect, monitor, record, download |
| `docs.html` | REST API reference — send to AI for context |

## Usage

1. Open `index.html` in a browser
2. Enter the device IP (AP: `192.168.4.1` or STA IP)
3. Click Connect

## API Docs

Click the &#9432; button (top-right) or open `docs.html` directly.
Share `docs.html` with AI assistants for API-aware help.

## Device Dependencies

- [power-meter-firmware](../power-meter-firmware/) (ESP32-S3)
- No build step required — pure static HTML
