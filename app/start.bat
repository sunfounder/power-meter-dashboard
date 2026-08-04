@echo off
chcp 65001 >nul
set DIR=%~dp0
set DASHBOARD=%DIR%..\power-meter-dashboard\index.html

:: Try Chrome
for /f "tokens=*" %%f in ('where chrome') do (
  start "" "%%f" --disable-web-security --user-data-dir="%TEMP%\pm-chrome" --allow-running-insecure-content "%DASHBOARD%"
  exit /b
)

:: Try Edge
for /f "tokens=*" %%f in ('where msedge') do (
  start "" "%%f" --disable-web-security --user-data-dir="%TEMP%\pm-edge" --allow-running-insecure-content "%DASHBOARD%"
  exit /b
)

:: Fallback: open with default browser
start "" "%DASHBOARD%"
echo HTTPS 可能会阻止连接，请用 Chrome/Edge 重新运行此脚本。
pause
