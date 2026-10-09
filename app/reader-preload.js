// Bridge for the hidden hand-reader window.
const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("readerApp", {
  config: () => ipcRenderer.invoke("reader:config"),
  templates: () => ipcRenderer.invoke("reader:templates"),
  source: (skipWindow) => ipcRenderer.invoke("reader:source", !!skipWindow),
  hand: (ev) => ipcRenderer.send("reader:hand", ev),
  status: (s) => ipcRenderer.send("reader:status", s),
  hudSpec: () => ipcRenderer.invoke("reader:hudspec"),
  turn: (ev) => ipcRenderer.send("reader:turn", ev),
  lp: (who, png) => ipcRenderer.invoke("reader:lp", who, png)
});
