# 发版一条龙：同步 web→app → 升版本 → 打包 zip → 创建 GitHub Release
# 用法: powershell -File scripts/release.ps1 1.2.3 "changelog 第一行" "changelog 第二行..."
param(
  [Parameter(Mandatory=$true)][string]$Version,
  [Parameter(Mandatory=$false)][string]$Notes
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$ver = $Version.TrimStart('v')

# ── 0. 检查 gh 登录 ──
gh auth status | Out-Null
if (-not $?) { Write-Error "gh 未登录: 先运行 gh auth login"; exit 1 }

# ── 1. 升版本号（UTF-8 无 BOM，避免编码损坏）──
$idx = Join-Path $root 'web\index.html'
$raw = [System.IO.File]::ReadAllText($idx, [System.Text.Encoding]::UTF8)
$raw = $raw -replace "const DASH_VER='[^']*'", "const DASH_VER='$ver'"
$raw = $raw -replace "Dashboard v[0-9.]+", "Dashboard v$ver"
[System.IO.File]::WriteAllText($idx, $raw, [System.Text.UTF8Encoding]::new($false))
Write-Host "[1/5] version -> $ver"

# ── 2. 同步 web → app/dashboard ──
$dst = Join-Path $root 'app\dashboard'
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item (Join-Path $root 'web\index.html')    $dst -Force
Copy-Item (Join-Path $root 'web\docs.html')     $dst -Force
Copy-Item (Join-Path $root 'web\manifest.json') $dst -Force
Copy-Item (Join-Path $root 'web\sw.js')         $dst -Force
Copy-Item (Join-Path $root 'web\version.json')  $dst -Force
Write-Host "[2/5] web -> app/dashboard synced"

# ── 3. git 提交 + tag ──
Push-Location $root
git add -A
git commit -m "v$ver" | Out-Null
git push origin main | Out-Null
git tag "v$ver"
git push origin "v$ver" | Out-Null
Pop-Location
Write-Host "[3/5] committed + tagged v$ver"

# ── 4. 打包（去掉 win-unpacked 层级）──
$env:CSC_IDENTITY_AUTO_DISCOVERY = 'false'
Push-Location (Join-Path $root 'app')
npm run build | Out-Null
Pop-Location
$unpacked = Join-Path $root 'app\dist\win-unpacked'
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zip = Join-Path $dist "SunFounderPowerMeter_v${ver}_win64.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
# zip 内直接是文件（无 win-unpacked 目录层）
Compress-Archive -Path (Join-Path $unpacked '*') -DestinationPath $zip -Force
Write-Host "[4/5] $zip ($([math]::Round((Get-Item $zip).Length/1MB,1)) MB)"

# ── 5. GitHub Release ──
Push-Location $root
if ([string]::IsNullOrEmpty($Notes)) { $Notes = "v$ver 自动发布 ($(Get-Date -Format 'yyyy-MM-dd'))" }
gh release create "v$ver" $zip --title "v$ver" --notes "## v$ver`n$Notes"
Pop-Location
Write-Host "[5/5] Release v$ver created"
