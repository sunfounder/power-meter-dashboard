const { app, BrowserWindow, ipcMain, dialog } = require('electron');
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
  mainWindow.loadFile('../power-meter-dashboard/index.html');
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
