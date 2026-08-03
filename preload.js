const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('electron', {
  log: (msg) => ipcRenderer.send('log', msg),
  apiLog: (data) => ipcRenderer.send('api-log', data),
  saveLog: () => ipcRenderer.invoke('save-log'),
  getLogPath: () => ipcRenderer.invoke('get-log-path')
});
