const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const path = require('path');
const fs = require('fs');

// ECharts needs eval (bundled build) → CSP allows unsafe-eval; silence the dev warning
// (packaged builds don't show security warnings anyway)
process.env.ELECTRON_DISABLE_SECURITY_WARNINGS = '1';

let mainWindow, logBuf = [];

app.commandLine.appendSwitch('disable-web-security');

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1200, height: 850, frame: false,
    autoHideMenuBar: true,
    webPreferences: {
      // All device traffic is WebSocket now — no need to disable webSecurity.
      // (GitHub release API allows CORS, so the update check still works.)
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js')
    }
  });
  mainWindow.maximize();
  mainWindow.loadFile(path.join(__dirname, 'dashboard/index.html'));
  mainWindow.setTitle('SunFounder 功率计');
}

app.whenReady().then(createWindow);
app.on('window-all-closed', () => app.quit());

// Logging — buffered in memory, only written to file on export
function log(msg) {
  logBuf.push(`${new Date().toISOString()} ${msg}`);
  if (logBuf.length > 2000) logBuf.shift();
  console.log(msg);
}

// IPC: window controls
ipcMain.on('min', () => mainWindow.minimize());
ipcMain.on('max', () => mainWindow.isMaximized() ? mainWindow.unmaximize() : mainWindow.maximize());
ipcMain.on('close', () => mainWindow.close());
ipcMain.on('devtools', () => mainWindow.webContents.openDevTools());

// IPC: log from renderer
ipcMain.on('log', (e, msg) => log('[WEB] ' + msg));
ipcMain.on('api-log', (e, data) => log('[API] ' + JSON.stringify(data)));

// IPC: save log to chosen path (buffered only, no auto-write)
ipcMain.handle('save-log', async () => {
  const d = new Date();
  const ts = `${d.getFullYear()}${('0'+(d.getMonth()+1)).slice(-2)}${('0'+d.getDate()).slice(-2)}_${('0'+d.getHours()).slice(-2)}${('0'+d.getMinutes()).slice(-2)}${('0'+d.getSeconds()).slice(-2)}`;
  const r = await dialog.showSaveDialog({
    defaultPath: path.join(app.getPath('desktop'), `power-meter_${ts}.log`),
    filters: [{ name: 'Log', extensions: ['log'] }]
  });
  if (!r.canceled) {
    fs.writeFileSync(r.filePath, logBuf.join('\n') + '\n');
    return r.filePath;
  }
  return null;
});
ipcMain.handle('open-external', (e, url) => { shell.openExternal(url); return true; });

// ── Update: background download → apply (extract+swap+restart) ──
const https = require('https');
const { spawn, exec } = require('child_process');

const UPD_DIR = () => path.join(app.getPath('temp'), 'pm_update');
const progress = (pct, status) => { try { mainWindow.webContents.send('update-progress', { pct, status }); } catch (_) {} };

// Phase 1: download zip in background (button shows progress, no overlay)
ipcMain.handle('download-update', async (e, url) => {
  if (!app.isPackaged) return { ok: false, error: 'dev mode' };
  const workDir = UPD_DIR();
  try { fs.rmSync(workDir, { recursive: true, force: true }); } catch (_) {}
  fs.mkdirSync(workDir, { recursive: true });
  const zipPath = path.join(workDir, 'update.zip');
  try {
    const download = (u, dest) => new Promise((resolve, reject) => {
      const req = https.get(u, res => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
          res.resume();
          download(new URL(res.headers.location, u).href, dest).then(resolve).catch(reject);
          return;
        }
        if (res.statusCode !== 200) { reject(new Error('HTTP ' + res.statusCode)); return; }
        const total = parseInt(res.headers['content-length'] || '0', 10);
        let got = 0;
        const f = fs.createWriteStream(dest);
        res.on('data', chunk => {
          got += chunk.length;
          if (total) progress(Math.min(99, Math.round(got / total * 100)), 'downloading');
        });
        res.pipe(f);
        res.on('end', () => { f.close(); resolve(); });
        f.on('error', reject);
      });
      req.on('error', reject);
      req.setTimeout(300000, () => req.destroy(new Error('timeout')));
    });
    await download(url, zipPath);
    log('[UPD] downloaded ' + Math.round(fs.statSync(zipPath).size / 1048576) + ' MB');
    return { ok: true, size: fs.statSync(zipPath).size };
  } catch (err) {
    log('[UPD] download failed: ' + err.message);
    return { ok: false, error: err.message };
  }
});

// Phase 2: apply already-downloaded zip (extract → updater.bat → restart)
ipcMain.handle('apply-update', async () => {
  if (!app.isPackaged) return { ok: false, error: 'dev mode' };
  const installDir = path.dirname(process.execPath);
  const workDir = UPD_DIR();
  const zipPath = path.join(workDir, 'update.zip');
  const staging = path.join(workDir, 'staging');
  if (!fs.existsSync(zipPath)) return { ok: false, error: 'no downloaded update' };

  try {
    progress(1, 'extracting');
    // extract via PowerShell Expand-Archive (no extra deps)
    const ps = `Expand-Archive -Path '${zipPath}' -DestinationPath '${staging}' -Force`;
    await new Promise((resolve, reject) => {
      exec(`powershell -NoProfile -Command "${ps.replace(/"/g, '\\"')}"`, err => err ? reject(err) : resolve());
    });
    progress(50, 'installing');

    // locate files inside staging (zip has no win-unpacked layer anymore)
    const src = staging;

    // updater.bat (lives in temp, not locked by target)
    const bat = path.join(workDir, 'updater.bat');
    const batContent = [
      '@echo off',
      'timeout /t 3 /nobreak >nul',
      `robocopy "${src}" "${installDir}" /E /R:5 /W:2 /NFL /NDL /NJH /NJS`,
      `for %%f in ("${installDir}\\*.exe") do start "" "%%f"`,
      `rmdir /s /q "${workDir}"`,
      'del "%~f0"'
    ].join('\r\n');
    fs.writeFileSync(bat, batContent);

    // launch updater detached, then quit
    spawn('cmd.exe', ['/c', bat], { detached: true, stdio: 'ignore', windowsHide: true }).unref();
    setTimeout(() => app.quit(), 800);
    return { ok: true };
  } catch (err) {
    log('[UPD] apply failed: ' + err.message);
    return { ok: false, error: err.message };
  }
});
