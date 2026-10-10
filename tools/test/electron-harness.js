// Loads app/main.js under a fake `electron` so the combo finder / generate lines / engine test IPC can be driven
// from plain Node against a locally built helper. Downloads are served from local files.
//
//   OMNI_ENGINE=combo-engine/build/combo-engine CE_SCRIPTS=CardScripts.zip CE_CDB=cards.cdb node -e '
//     const h = require("./tools/test/electron-harness.js");
//     h.handlers["combo:search"](null, {deck:[...], extra:[...], hand:[...], timeMs:3000}).then(r => console.log(r.boards.length));'
//
// Exports: handlers (ipcMain.handle), ons (ipcMain.on), events (everything sent to the page), userData (temp dir).
const Module = require("module"), path = require("path"), fs = require("fs"), os = require("os");
const UD = fs.mkdtempSync(path.join(os.tmpdir(), "mdc-ud-"));
const handlers = {}, ons = {}, events = [];
const fake = {
  app: { getPath: k => k === "userData" ? UD : os.tmpdir(), setPath() {}, isPackaged: false, getVersion: () => "0.0.0-test",
    requestSingleInstanceLock: () => true, on() {}, whenReady: () => new Promise(() => {}), commandLine: { appendSwitch() {} },
    disableHardwareAcceleration() {}, quit() {} },
  ipcMain: { handle: (n, f) => handlers[n] = f, on: (n, f) => ons[n] = f },
  net: { fetch: async (url) => {
    const f = url.includes("CardScripts") ? process.env.CE_SCRIPTS : url.includes("cards.cdb") ? process.env.CE_CDB : null;
    if (!f) return new Response("", { status: 404 });
    const buf = fs.readFileSync(f); return new Response(buf, { status: 200, headers: { "content-length": String(buf.length) } });
  } },
  BrowserWindow: function () {}, globalShortcut: {}, Tray: function () {}, Menu: {}, shell: {}, screen: {},
  protocol: { registerSchemesAsPrivileged() {}, handle() {} }, desktopCapturer: {}
};
const orig = Module._load;
Module._load = function (r, ...a) { return r === "electron" ? fake : orig.call(this, r, ...a); };
const mainPath = path.join(__dirname, "../../app/main.js");
const src = fs.readFileSync(mainPath, "utf8")
  .replace(/function pageEvent\(name, detail\) \{/, "function pageEvent(name, detail) { globalThis.__mdcEvent && globalThis.__mdcEvent(name, detail); return;");
globalThis.__mdcEvent = (name, detail) => events.push({ name, detail });
const m = new Module(mainPath); m.filename = mainPath; m.paths = Module._nodeModulePaths(path.dirname(mainPath));
m._compile(src, mainPath);
module.exports = { handlers, ons, events, userData: UD };
