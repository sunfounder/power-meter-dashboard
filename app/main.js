const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const path = require('path');
const fs = require('fs');

let mainWindow, logBuf = [];

app.commandLine.appendSwitch('disable-web-security');

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1200, height: 850, frame: false,
    autoHideMenuBar: true,
    webPreferences: {
      webSecurity: false,
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
ipcMain.handle('get-log-path', () => '');
ipcMain.handle('open-external', (e, url) => { shell.openExternal(url); return true; });

// ── Update: download zip → extract → updater.bat → restart ──
const https = require('https');
const { spawn, exec } = require('child_process');

ipcMain.handle('do-update', async (e, url) => {
  if (!app.isPackaged) return { ok: false, error: 'dev mode' };
  const installDir = path.dirname(process.execPath);
  const workDir = path.join(app.getPath('temp'), 'pm_update');
  try { fs.rmSync(workDir, { recursive: true, force: true }); } catch (_) {}
  fs.mkdirSync(workDir, { recursive: true });
  const zipPath = path.join(workDir, 'update.zip');
  const staging = path.join(workDir, 'staging');
  const progress = (pct, status) => { try { mainWindow.webContents.send('update-progress', { pct, status }); } catch (_) {} };

  try {
    // 1. download (follow redirects — GitHub download URLs 302 to objects.githubusercontent.com)
    const download = (url, dest) => new Promise((resolve, reject) => {
      const req = https.get(url, res => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
          res.resume();
          download(new URL(res.headers.location, url).href, dest).then(resolve).catch(reject);
          return;
        }
        if (res.statusCode !== 200) { reject(new Error('HTTP ' + res.statusCode)); return; }
        const total = parseInt(res.headers['content-length'] || '0', 10);
        let got = 0;
        const f = fs.createWriteStream(dest);
        res.on('data', chunk => {
          got += chunk.length;
          if (total) progress(Math.round(got / total * 90), 'downloading');
        });
        res.pipe(f);
        res.on('end', () => { f.close(); resolve(); });
        f.on('error', reject);
      });
      req.on('error', reject);
      req.setTimeout(180000, () => req.destroy(new Error('timeout')));
    });
    progress(5, 'downloading');
    await download(url, zipPath);
    progress(92, 'extracting');
    log('[UPD] downloaded ' + Math.round(fs.statSync(zipPath).size / 1048576) + ' MB');

    // 2. extract via PowerShell Expand-Archive (no extra deps)
    const ps = `Expand-Archive -Path '${zipPath}' -DestinationPath '${staging}' -Force`;
    await new Promise((resolve, reject) => {
      exec(`powershell -NoProfile -Command "${ps.replace(/"/g, '\\"')}"`, err => err ? reject(err) : resolve());
    });
    progress(96, 'installing');

    // 3. locate win-unpacked dir inside staging
    const sub = fs.readdirSync(staging).map(n => path.join(staging, n)).find(p => fs.statSync(p).isDirectory());
    const src = sub || staging;

    // 4. updater.bat (lives in temp, not locked by target)
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

    // 5. launch updater detached, then quit
    spawn('cmd.exe', ['/c', bat], { detached: true, stdio: 'ignore' }).unref();
    setTimeout(() => app.quit(), 800);
    return { ok: true };
  } catch (err) {
    log('[UPD] failed: ' + err.message);
    try { fs.rmSync(workDir, { recursive: true, force: true }); } catch (_) {}
    return { ok: false, error: err.message };
  }
});
