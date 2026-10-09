// Small, safe bridge so the overlay page and the hotkey editor can ask the app to do things.
const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("overlayApp", {
  snap: (side) => ipcRenderer.send("snap", side),
  layout: (mode) => ipcRenderer.send("layout", mode),
  openHotkeys: () => ipcRenderer.send("open-hotkeys"),
  getHotkeys: () => ipcRenderer.invoke("hk:get"),
  setHotkey: (action, accel) => ipcRenderer.invoke("hk:set", { action, accel }),
  resetHotkeys: () => ipcRenderer.invoke("hk:reset"),
  capturing: (on) => ipcRenderer.send("hk:capture", !!on),
  closeMe: () => ipcRenderer.send("hk:close"),
  installUpdate: () => ipcRenderer.send("update:install"),
  checkUpdate: () => ipcRenderer.send("update:check"),
  version: () => ipcRenderer.invoke("app:version")
});
