const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');
const fs = require('fs');

let mainWindow, logFile = '';

app.commandLine.appendSwitch('disable-web-security');

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1200, height: 850,
    autoHideMenuBar: true,
    webPreferences: {
      webSecurity: false,
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js')
    }
  });
  mainWindow.maximize();
  mainWindow.loadFile('../power-meter-dashboard/index.html');
  mainWindow.setTitle('SunFounder 功率计');
}

app.whenReady().then(createWindow);
app.on('window-all-closed', () => app.quit());

// Logging
function getLogPath() {
  if (!logFile) {
    const d = new Date();
    const ts = `${d.getFullYear()}${('0'+(d.getMonth()+1)).slice(-2)}${('0'+d.getDate()).slice(-2)}_${('0'+d.getHours()).slice(-2)}${('0'+d.getMinutes()).slice(-2)}`;
    logFile = path.join(app.getPath('desktop'), `power-meter_${ts}.log`);
  }
  return logFile;
}
function log(msg) {
  const line = `${new Date().toISOString()} ${msg}\n`;
  fs.appendFileSync(getLogPath(), line);
  console.log(msg);
}

// IPC: log from renderer
ipcMain.on('log', (e, msg) => log('[WEB] ' + msg));
ipcMain.on('api-log', (e, data) => log('[API] ' + JSON.stringify(data)));

// IPC: save log
ipcMain.handle('save-log', async () => {
  const p = getLogPath();
  const r = await dialog.showSaveDialog({ defaultPath: p, filters: [{ name: 'Log', extensions: ['log'] }] });
  if (!r.canceled) fs.copyFileSync(p, r.filePath);
  return r.canceled ? null : r.filePath;
});
ipcMain.handle('get-log-path', () => getLogPath());
