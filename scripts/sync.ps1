# 发版脚本：同步网页到 APP 副本 + 打 zip
# 用法: powershell -File scripts\sync.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# 1. 同步网页到 APP
$src = Join-Path $root 'web'
$dst = Join-Path $root 'app\dashboard'
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item (Join-Path $src 'index.html')    $dst -Force
Copy-Item (Join-Path $src 'docs.html')     $dst -Force
Copy-Item (Join-Path $src 'manifest.json') $dst -Force
Copy-Item (Join-Path $src 'sw.js')         $dst -Force
Copy-Item (Join-Path $src 'version.json')  $dst -Force
Write-Host "[1/3] web -> app/dashboard synced"

# 2. 打包 APP（文件夹版）
$env:CSC_IDENTITY_AUTO_DISCOVERY = 'false'
Push-Location (Join-Path $root 'app')
npm run build
Pop-Location
Write-Host "[2/3] app built"

# 3. 压缩 zip
$ver = (Get-Content (Join-Path $src 'version.json') | ConvertFrom-Json).version
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zip = Join-Path $dist "SunFounderPowerMeter_v${ver}_win64.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $root 'app\dist\win-unpacked') -DestinationPath $zip -Force
Write-Host "[3/3] $zip ($([math]::Round((Get-Item $zip).Length/1MB,1)) MB)"
