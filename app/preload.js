const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('electron', {
  log: (msg) => ipcRenderer.send('log', msg),
  apiLog: (data) => ipcRenderer.send('api-log', data),
  saveLog: () => ipcRenderer.invoke('save-log'),
  getLogPath: () => ipcRenderer.invoke('get-log-path'),
  min: () => ipcRenderer.send('min'),
  max: () => ipcRenderer.send('max'),
  close: () => ipcRenderer.send('close'),
  openExternal: (url) => ipcRenderer.invoke('open-external', url),
  doUpdate: (url) => ipcRenderer.invoke('do-update', url)
});
