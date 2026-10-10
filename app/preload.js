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
  theme: () => ipcRenderer.invoke("hk:theme"),
  installUpdate: () => ipcRenderer.send("update:install"),
  checkUpdate: () => ipcRenderer.send("update:check"),
  version: () => ipcRenderer.invoke("app:version"),
  getSettings: () => ipcRenderer.invoke("settings:get"),
  setSetting: (key, value) => ipcRenderer.send("settings:set", { key, value }),
  setPassthrough: (on) => ipcRenderer.send("mouse:passthrough", !!on),
  comboAvailable: () => ipcRenderer.invoke("combo:available"),
  findCombos: (q) => ipcRenderer.invoke("combo:search", q),
  stopCombos: () => ipcRenderer.send("combo:stop"),
  genStart: (job) => ipcRenderer.invoke("gen:start", job),
  genStatus: () => ipcRenderer.invoke("gen:status"),
  genTake: (deckId) => ipcRenderer.invoke("gen:take", deckId),
  genCancel: () => ipcRenderer.send("gen:cancel"),
  benchRun: (spec) => ipcRenderer.invoke("bench:run", spec),
  benchStop: () => ipcRenderer.send("bench:stop")
});
