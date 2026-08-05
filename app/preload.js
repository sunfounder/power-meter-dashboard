const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('electron', {
  log: (msg) => ipcRenderer.send('log', msg),
  apiLog: (data) => ipcRenderer.send('api-log', data),
  saveLog: () => ipcRenderer.invoke('save-log'),
  min: () => ipcRenderer.send('min'),
  max: () => ipcRenderer.send('max'),
  close: () => ipcRenderer.send('close'),
  devtools: () => ipcRenderer.send('devtools'),
  openExternal: (url) => ipcRenderer.invoke('open-external', url),
  downloadUpdate: (url) => ipcRenderer.invoke('download-update', url),
  applyUpdate: () => ipcRenderer.invoke('apply-update'),
  onUpdateProgress: (cb) => ipcRenderer.on('update-progress', (e, p) => cb(p))
});
