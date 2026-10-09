// Master Duel Companion App: wraps the companion in an always-on-top window with global hotkeys.
const { app, BrowserWindow, globalShortcut, Tray, Menu, shell, screen, ipcMain, protocol, net, desktopCapturer } = require("electron");
const { pathToFileURL } = require("url");
const path = require("path");
const fs = require("fs");

const SITE_URL = process.env.OMNI_SITE || "https://gabegriffin16-commits.github.io/ygo-md-companion/";
const SITE_DIR = path.join(__dirname, "site");
// The bundled copy is served as app://site/ so the page can load deck.json, rules.json and cards.json
// (browsers block fetch() from file:// pages).
const LOCAL_URL = "app://site/index.html";
protocol.registerSchemesAsPrivileged([{ scheme: "app", privileges: { standard: true, secure: true, supportFetchAPI: true } }]);
// Keep using the old settings folder so hotkeys, window spot and card cache carry over after the rename.
app.setPath("userData", path.join(app.getPath("appData"), "Omni HERO Overlay"));
const SETTINGS_FILE = path.join(app.getPath("userData"), "settings.json");

const DEFAULT_HOTKEYS = {
  toggle:       "Control+Shift+O",    // show / hide the overlay
  clickThrough: "Control+Shift+L",    // lock: clicks pass through to the game
  opacityUp:    "Control+Shift+Up",
  opacityDown:  "Control+Shift+Down",
  compact:      "Control+Shift+C",    // compact / full view
  next:         "Control+Shift+N",    // next duel step (Draw -> Main -> End -> next turn)
  undo:         "Control+Shift+Z",    // undo last play
  ash:          "Control+Shift+A",    // mark Ash on the current trigger / last play
  imp:          "Control+Shift+I",    // mark Imperm
  neg:          "Control+Shift+X",    // mark other negate
  find:         "Control+Shift+F",    // jump to card search
  snapLeft:     "Control+Shift+Left", // snap to the left edge (again: walk to the next edge / monitor)
  snapRight:    "Control+Shift+Right",
  expand:       "Control+Shift+E"     // full view across the monitor / back to compact
};
const DEFAULTS = { bounds: null, opacity: 0.95, clickThrough: false, source: "online", hotkeys: DEFAULT_HOTKEYS, firstRun: true, reader: true, seeThrough: true, coverTaskbar: "focus",
  layout: { mode: "compact", side: "right", display: -1 } };

let settings = loadSettings();
let win = null, tray = null, saveTimer = null;

function loadSettings() {
  try {
    const s = JSON.parse(fs.readFileSync(SETTINGS_FILE, "utf8"));
    return Object.assign({}, DEFAULTS, s, { hotkeys: Object.assign({}, DEFAULT_HOTKEYS, s.hotkeys || {}) });
  } catch { return JSON.parse(JSON.stringify(DEFAULTS)); }
}
function saveSettings() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    try { fs.mkdirSync(path.dirname(SETTINGS_FILE), { recursive: true }); fs.writeFileSync(SETTINGS_FILE, JSON.stringify(settings, null, 2)); } catch {}
  }, 300);
}

function send(cmd) {
  if (!win) return;
  win.webContents.executeJavaScript(`window.dispatchEvent(new CustomEvent("overlay-cmd",{detail:${JSON.stringify(cmd)}}))`).catch(() => {});
}

function applyClickThrough() {
  if (!win) return;
  win.setIgnoreMouseEvents(!!settings.clickThrough, { forward: true });
  win.webContents.executeJavaScript(`document.documentElement.classList.toggle("ov-locked", ${!!settings.clickThrough})`).catch(() => {});
  buildTrayMenu();
}
function setOpacity(v) {
  settings.opacity = Math.min(1, Math.max(0.3, Math.round(v * 100) / 100));
  if (win) win.setOpacity(settings.opacity);
  saveSettings(); buildTrayMenu();
}
function toggleVisible() {
  if (!win) return;
  if (win.isVisible()) win.hide();
  else win.showInactive(); // don't steal focus from the game
}

function overlayChrome() {
  if (update) setTimeout(() => updateEvent({ state: "available", version: update.version, notes: update.notes }), 1500);
  // A thin drag bar plus overlay styles, injected into the companion page.
  const css = `
    #ovBar{position:fixed;top:0;left:0;right:0;height:16px;z-index:9999;display:flex;justify-content:center;align-items:center;
      background:linear-gradient(var(--panel,#262058),transparent);font:600 10px sans-serif;color:var(--muted,#aaa);letter-spacing:.08em}
    #ovBar span{opacity:.7}
    #ovBar .ovb{-webkit-app-region:no-drag;position:absolute;top:0;height:16px;width:22px;border:0;background:none;color:inherit;font:700 11px sans-serif;cursor:pointer;opacity:.75;padding:0}
    #ovBar .ovb:hover{opacity:1;color:var(--gold,#E0B85E)}
    #ovBar #ovL{left:4px} #ovBar #ovR{left:26px} #ovBar #ovF{left:48px} #ovBar #ovK{right:6px}
    html.in-overlay body{box-sizing:border-box;padding-top:16px !important}
    @media (min-width:1220px){ html.in-overlay .layout{height:calc(100vh - 16px) !important} }
    *{scrollbar-width:thin;scrollbar-color:color-mix(in srgb,var(--gold,#E0B85E) 55%,transparent) transparent}
    ::-webkit-scrollbar{width:6px;height:6px} ::-webkit-scrollbar-track{background:transparent}
    ::-webkit-scrollbar-thumb{background:color-mix(in srgb,var(--gold,#E0B85E) 45%,transparent);border-radius:999px}
    .top{-webkit-app-region:drag} .top button,.top a,.top input{-webkit-app-region:no-drag}
    html.ov-locked #ovBar{background:var(--red,#d2606f);color:#fff}
    html.ov-locked #ovBar span::after{content:"  LOCKED: clicks go to the game (Ctrl+Shift+L)"}
`;
  win.webContents.insertCSS(css).catch(() => {});
  win.webContents.executeJavaScript(`
    if(!document.getElementById("ovBar")){var b=document.createElement("div");b.id="ovBar";
      b.innerHTML='<button class="ovb" id="ovL" title="Snap left">\\u25E7</button><button class="ovb" id="ovR" title="Snap right">\\u25E8</button><button class="ovb" id="ovF" title="Full view / compact">\\u2922</button><span>\\u2630 MASTER DUEL COMPANION</span><button class="ovb" id="ovK" title="Hotkeys">\\u2699</button>';
      document.body.appendChild(b);
      if(window.overlayApp){document.getElementById("ovL").onclick=function(){overlayApp.snap("left")};document.getElementById("ovR").onclick=function(){overlayApp.snap("right")};document.getElementById("ovK").onclick=function(){overlayApp.openHotkeys()};document.getElementById("ovF").onclick=function(){var c=document.getElementById("compactBtn");if(c)c.click()};}}
    document.documentElement.classList.add("in-overlay");
    document.documentElement.classList.toggle("ov-locked", ${!!settings.clickThrough});
    ${settings.firstRun ? `if(!document.documentElement.classList.contains("compact")){var c=document.getElementById("compactBtn"); if(c) c.click();}` : ""}
  `).catch(() => {});
  if (settings.firstRun) { settings.firstRun = false; saveSettings(); }
}

function loadPage() {
  if (settings.source === "local") win.loadURL(LOCAL_URL);
  else win.loadURL(SITE_URL);
}

function createWindow() {
  const b = layoutBounds();
  win = new BrowserWindow({
    x: b.x, y: b.y, width: b.width, height: b.height,
    frame: false, alwaysOnTop: true, show: false, resizable: false, movable: false, maximizable: false, fullscreenable: false,
    // See-through window: the page paints its own background, so "Fade background" in the theme menu can let the game show through.
    transparent: !!settings.seeThrough, backgroundColor: settings.seeThrough ? "#00000000" : "#1B1640",
    title: "Master Duel Companion App", icon: path.join(__dirname, "icon.ico"),
    webPreferences: { contextIsolation: true, nodeIntegration: false, backgroundThrottling: false, preload: path.join(__dirname, "preload.js") }
  });
  win.setAlwaysOnTop(true, "screen-saver"); // stay above a borderless-windowed game
  win.setOpacity(settings.opacity);
  win.once("ready-to-show", () => { win.showInactive(); applyClickThrough(); });
  win.webContents.on("did-finish-load", overlayChrome);
  if (process.env.OMNI_TEST_SHOT) win.webContents.on("did-finish-load", () => setTimeout(() => {
    win.webContents.capturePage().then(img => fs.writeFileSync(process.env.OMNI_TEST_SHOT, img.toPNG()));
  }, Number(process.env.OMNI_TEST_SHOT_MS) || 5000));
  if (process.env.OMNI_TEST_SCRIPT) win.webContents.on("did-finish-load", () => {
    win.webContents.executeJavaScript(fs.readFileSync(process.env.OMNI_TEST_SCRIPT, "utf8")).catch(() => {});
  });
  win.webContents.on("did-fail-load", (e, code, desc, url, isMain) => {
    if (isMain && settings.source !== "local" && !url.startsWith("app:")) win.loadURL(LOCAL_URL); // offline: use the bundled copy
  });
  win.webContents.setWindowOpenHandler(({ url }) => { shell.openExternal(url); return { action: "deny" }; });
  screen.on("display-metrics-changed", applyLayout); screen.on("display-removed", applyLayout); screen.on("display-added", applyLayout);
  loadPage();
}

const ACTIONS = {
  toggle: toggleVisible,
  clickThrough: () => { settings.clickThrough = !settings.clickThrough; saveSettings(); applyClickThrough(); if (win && !win.isVisible()) win.showInactive(); },
  opacityUp: () => setOpacity(settings.opacity + 0.1),
  opacityDown: () => setOpacity(settings.opacity - 0.1),
  compact: () => send("compact"),
  next: () => send("next"),
  undo: () => send("undo"),
  ash: () => send("ash"),
  imp: () => send("imp"),
  neg: () => send("neg"),
  snapLeft: () => snap("left"),
  snapRight: () => snap("right"),
  expand: () => send("compact"),
  find: () => { if (!win) return; if (settings.clickThrough) { settings.clickThrough = false; applyClickThrough(); } win.show(); win.focus(); send("find"); }
};
let failedKeys = [];
function registerHotkeys() {
  globalShortcut.unregisterAll(); failedKeys = [];
  for (const [action, accel] of Object.entries(settings.hotkeys)) {
    if (!accel || !ACTIONS[action]) continue;
    try { if (!globalShortcut.register(accel, ACTIONS[action])) failedKeys.push(accel); } catch { failedKeys.push(accel); }
  }
  buildTrayMenu();
}

function buildTrayMenu() {
  if (!tray) return;
  const k = settings.hotkeys;
  const pct = Math.round(settings.opacity * 100);
  tray.setToolTip("Master Duel Companion App" + (failedKeys.length ? ` (hotkeys in use elsewhere: ${failedKeys.join(", ")})` : ""));
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: `Show / hide  (${k.toggle})`, click: toggleVisible },
    { label: `Click-through lock  (${k.clickThrough})`, type: "checkbox", checked: !!settings.clickThrough, click: ACTIONS.clickThrough },
    { label: `Opacity: ${pct}%`, submenu: [100, 90, 80, 70, 60, 50].map(v => ({ label: v + "%", type: "radio", checked: pct === v, click: () => setOpacity(v / 100) })) },
    { type: "separator" },
    { label: "Use online version (always current)", type: "radio", checked: settings.source !== "local", click: () => { settings.source = "online"; saveSettings(); loadPage(); buildTrayMenu(); } },
    { label: "Use offline copy (bundled)", type: "radio", checked: settings.source === "local", click: () => { settings.source = "local"; saveSettings(); loadPage(); buildTrayMenu(); } },
    { label: "Reload page", click: () => win && win.reload() },
    { label: "See-through window (restarts the app)", type: "checkbox", checked: !!settings.seeThrough, click: () => { settings.seeThrough = !settings.seeThrough; saveSettings(); setTimeout(() => { const pe = process.env.PORTABLE_EXECUTABLE_FILE; app.relaunch(pe ? { execPath: pe, args: [] } : undefined); app.exit(0); }, 400); } },
    { type: "separator" },
    { label: "Read my hand from the screen", type: "checkbox", checked: !!settings.reader, click: () => { settings.reader = !settings.reader; saveSettings(); settings.reader ? startReader() : stopReader(); buildTrayMenu(); } },
    { label: `Hand reader: ${readerState.label || "off"}`, enabled: false },
    { type: "separator" },
    { label: "Cover the taskbar", submenu: [
      { label: "When Master Duel is focused", type: "radio", checked: (settings.coverTaskbar || "focus") === "focus", click: () => setCover("focus") },
      { label: "Always", type: "radio", checked: settings.coverTaskbar === "always", click: () => setCover("always") },
      { label: "Never", type: "radio", checked: settings.coverTaskbar === "never", click: () => setCover("never") } ] },
    { label: "Reset position (compact, right side)", click: () => { settings.layout = { mode: settings.layout.mode, side: "right", display: -1 }; saveSettings(); if (settings.layout.mode === "full") send("compact"); else applyLayout(); } },
    { label: `Full view / compact  (${k.expand})`, click: () => send("compact") },
    { label: `Snap left  (${k.snapLeft})`, click: () => snap("left") },
    { label: `Snap right  (${k.snapRight})`, click: () => snap("right") },
    { label: "Hotkeys…", click: openHotkeys },
    { type: "separator" },
    update ? { label: `Update to v${update.version}`, click: installUpdate } : { label: `Check for updates (v${app.getVersion()})`, click: () => checkUpdate(true) },
    { label: "Quit", click: () => app.quit() }
  ]));
}

// ---------- updates ----------
// GitHub builds the exe whenever the app changes and posts it as a release. On start (and every few hours)
// the app asks GitHub for the latest release; if it's newer, an Update button shows in the app bar.
// Updating downloads the new exe next to the current one, quits, swaps the files and starts the new version.
const REPO = "gabegriffin16-commits/ygo-md-companion";
let update = null, updating = false;
function newerThan(a, b) {
  const pa = String(a).split(".").map(Number), pb = String(b).split(".").map(Number);
  for (let i = 0; i < 3; i++) if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) > (pb[i] || 0);
  return false;
}
function updateEvent(d) {
  if (process.env.OMNI_DEBUG) console.log("[update]", JSON.stringify(d));
  pageEvent("overlay-update", Object.assign({ current: app.getVersion() }, d));
}
async function checkUpdate(manual) {
  try {
    const r = await net.fetch(`https://api.github.com/repos/${REPO}/releases/latest`,
      { headers: { "User-Agent": "MasterDuelCompanion", "Accept": "application/vnd.github+json" }, cache: "no-store" });
    if (!r.ok) throw new Error("GitHub said " + r.status);
    const j = await r.json(), v = String(j.tag_name || "").replace(/^v/, "");
    const asset = (j.assets || []).find(a => /\.exe$/i.test(a.name));
    if (asset && newerThan(v, app.getVersion())) {
      update = { version: v, url: asset.browser_download_url, size: asset.size, notes: (j.body || "").slice(0, 400) };
      updateEvent({ state: "available", version: v, notes: update.notes });
    } else { update = null; if (manual) updateEvent({ state: "current" }); }
  } catch (e) { if (manual) updateEvent({ state: "error", note: e.message }); }
  buildTrayMenu();
}
async function installUpdate() {
  if (!update || updating) return;
  const exe = process.env.PORTABLE_EXECUTABLE_FILE;
  if (!exe) { shell.openExternal(`https://github.com/${REPO}/releases/latest`); return; }   // not the portable exe (dev run)
  updating = true;
  const tmp = exe + ".new";
  try {
    updateEvent({ state: "downloading", version: update.version, pct: 0 });
    const r = await net.fetch(update.url, { headers: { "User-Agent": "MasterDuelCompanion" } });
    if (!r.ok) throw new Error("download failed (" + r.status + ")");
    const total = +r.headers.get("content-length") || update.size || 0;
    const out = fs.createWriteStream(tmp), rd = r.body.getReader();
    let got = 0, lastPct = -1;
    for (;;) {
      const { done, value } = await rd.read();
      if (done) break;
      got += value.length;
      if (!out.write(Buffer.from(value))) await new Promise(res => out.once("drain", res));
      const pct = total ? Math.floor(got * 100 / total) : 0;
      if (pct !== lastPct && pct % 2 === 0) { lastPct = pct; updateEvent({ state: "downloading", version: update.version, pct }); }
    }
    await new Promise((res, rej) => { out.on("error", rej); out.end(res); });
    if (total && fs.statSync(tmp).size !== total) throw new Error("download was incomplete");
    // Swap once this app (and the portable launcher around it) has closed, then start the new one.
    const cmd = path.join(app.getPath("temp"), "mdc-update.cmd");
    fs.writeFileSync(cmd, [
      "@echo off", "set tries=0", ":wait", "timeout /t 1 /nobreak >nul",
      `move /y "${tmp}" "${exe}" >nul 2>&1`, "if not errorlevel 1 goto go",
      "set /a tries+=1", "if %tries% lss 60 goto wait", ":go", `start "" "${exe}"`, `del "%~f0"`, ""].join("\r\n"));
    await new Promise((res, rej) => {
      const ch = require("child_process").spawn("cmd.exe", ["/c", cmd], { detached: true, stdio: "ignore", windowsHide: true });
      ch.once("error", rej); ch.once("spawn", () => { ch.unref(); res(); });
    });
    updateEvent({ state: "restarting", version: update.version });
    setTimeout(() => app.quit(), 600);
  } catch (e) {
    updating = false;
    try { fs.unlinkSync(tmp); } catch {}
    updateEvent({ state: "error", note: e.message, version: update && update.version });
  }
}
ipcMain.on("update:install", () => installUpdate());
ipcMain.on("update:check", () => checkUpdate(true));
ipcMain.handle("app:version", () => app.getVersion());

// ---------- hand reader ----------
// A hidden window watches the Master Duel window, recognizes the cards in your hand,
// and tells the companion page what changed. It only looks at pixels; it never touches the game.
let readerWin = null, readerState = { label: "off" };
const CARD_CACHE = path.join(app.getPath("userData"), "cards");
function pageEvent(name, detail) {
  if (!win) return;
  win.webContents.executeJavaScript(`window.dispatchEvent(new CustomEvent(${JSON.stringify(name)},{detail:${JSON.stringify(detail)}}))`).catch(() => {});
}
function startReader() {
  if (readerWin || !settings.reader) return;
  readerWin = new BrowserWindow({ show: false, width: 400, height: 300, webPreferences: {
    preload: path.join(__dirname, "reader-preload.js"), contextIsolation: true, nodeIntegration: false, backgroundThrottling: false } });
  readerWin.loadFile(path.join(__dirname, "reader.html"));
  readerWin.on("closed", () => { readerWin = null; });
  if (process.env.OMNI_DEBUG) readerWin.webContents.on("console-message", (e, lvl, msg) => console.log("[reader]", msg));
}
function stopReader() {
  if (readerWin) readerWin.destroy();
  readerWin = null; readerState = { label: "off" }; pageEvent("overlay-reader", { state: "off" });
}
async function cardBytes(cid) {
  const file = path.join(CARD_CACHE, cid + ".jpg");
  try { return fs.readFileSync(file); } catch {}
  for (const url of [SITE_URL + "images/" + cid + ".jpg", "https://images.ygoprodeck.com/images/cards/" + cid + ".jpg"]) {
    try {
      const r = await net.fetch(url);
      if (!r.ok) continue;
      const buf = Buffer.from(await r.arrayBuffer());
      try { fs.mkdirSync(CARD_CACHE, { recursive: true }); fs.writeFileSync(file, buf); } catch {}
      return buf;
    } catch {}
  }
  return null;
}
ipcMain.handle("reader:config", () => ({ testVideo: process.env.OMNI_READER_VIDEO ? pathToFileURL(process.env.OMNI_READER_VIDEO).toString() : null,
  rate: +process.env.OMNI_READER_RATE || 1 }));
ipcMain.handle("reader:templates", async () => {
  if (!win) return [];
  // The companion page publishes the main deck (key, name, card id) as window.OMNI_DECK.
  const deck = await win.webContents.executeJavaScript("window.OMNI_DECK || null").catch(() => null);
  if (!deck || !deck.length) return [];
  const out = [];
  for (const c of deck) {
    if (!c.cid) continue;
    const bytes = await cardBytes(c.cid);
    if (bytes) out.push({ key: c.key, name: c.name, bytes });
  }
  return out;
});
let screenTry = 0;
ipcMain.handle("reader:source", async (e, skipWindow) => {
  const srcs = await desktopCapturer.getSources({ types: ["window", "screen"], thumbnailSize: { width: 0, height: 0 } });
  const game = srcs.find(s => /master\s*duel|masterduel/i.test(s.name) && s.id.startsWith("window"));
  if (!game) return null;   // game isn't running: wait
  if (!skipWindow) return { id: game.id, name: "Master Duel window" };
  const screens = srcs.filter(s => s.id.startsWith("screen"));
  if (!screens.length) return null;
  const s = screens[screenTry++ % screens.length];
  return { id: s.id, name: s.name || "screen" };
});
ipcMain.handle("reader:hudspec", () => JSON.parse(fs.readFileSync(path.join(__dirname, "hud-templates.json"), "utf8")));
ipcMain.on("reader:turn", (e, ev) => { if (process.env.OMNI_DEBUG) console.log("[turn]", JSON.stringify(ev)); pageEvent("overlay-turn", ev); });
// LP numbers: OCR on the small black-on-white crop the reader sends. Tesseract runs here in a worker thread.
let ocr = null;
function unpacked(p) { return p.replace("app.asar" + path.sep, "app.asar.unpacked" + path.sep); }
async function ocrWorker() {
  if (ocr) return ocr;
  ocr = (async () => {
    const T = require("tesseract.js");
    const ddir = path.dirname(require.resolve("@tesseract.js-data/eng/package.json"));
    const w = await T.createWorker("eng", 1, { workerPath: unpacked(path.join(__dirname, "tess-worker.js")),
      langPath: unpacked(path.join(ddir, "4.0.0_best_int")), gzip: true, cacheMethod: "none" });
    await w.setParameters({ tessedit_pageseg_mode: "7", tessedit_char_whitelist: "0123456789" });
    return w;
  })();
  ocr.catch(() => { ocr = null; });
  return ocr;
}
const lastLP = {};
ipcMain.handle("reader:lp", async (e, who, dataUrl) => {
  try {
    const w = await Promise.race([ocrWorker(), new Promise((_, rej) => setTimeout(() => rej(new Error("ocr start timeout")), 20000))]);
    const r = await Promise.race([w.recognize(Buffer.from(dataUrl.split(",")[1], "base64")), new Promise((_, rej) => setTimeout(() => rej(new Error("ocr timeout")), 8000))]);
    const txt = (r.data.text || "").replace(/\D/g, ""), conf = r.data.confidence;
    if (process.env.OMNI_DEBUG) console.log("[lp]", who, JSON.stringify(r.data.text.trim()), Math.round(conf));
    if (!/^\d{1,5}$/.test(txt) || conf < 55) return null;
    const lp = +txt;
    if (lastLP[who] !== lp) { lastLP[who] = lp; pageEvent("overlay-lp", { who, lp }); }
    return lp;
  } catch (err) { if (process.env.OMNI_DEBUG) console.log("[lp] error", err.message); return null; }
});
ipcMain.on("reader:hand", (e, ev) => { if (process.env.OMNI_DEBUG) console.log("[hand]", ev.order.join(","), "added:", ev.added.join(",")); pageEvent("overlay-hand", ev); });
ipcMain.on("reader:status", (e, s) => {
  if (process.env.OMNI_DEBUG && s.state !== "reading") console.log("[status]", JSON.stringify(s));
  const label = s.state === "reading" ? "reading" + (s.source ? " (" + s.source + ")" : "") : s.state === "searching" ? "waiting for Master Duel" : s.state;
  if (label !== readerState.label) { readerState = { label }; buildTrayMenu(); }
  pageEvent("overlay-reader", s);
  // Test runs only: dump the duel state when the test video ends.
  if (s.state === "done" && process.env.OMNI_TEST_DUMP && win) {
    win.webContents.executeJavaScript("localStorage.getItem('hbc-duel')").then(d => {
      fs.writeFileSync(process.env.OMNI_TEST_DUMP, d || ""); app.quit();
    });
  }
});

// ---------- layout: fixed sizes, no dragging ----------
// Compact docks a 540px column to the left or right edge of a monitor, full height.
// Full view fills the whole monitor. Snap left/right walks the edges across monitors.
const COMPACT_W = 540;
function displaysLR() { return screen.getAllDisplays().slice().sort((a, b) => a.workArea.x - b.workArea.x); }
function curDisplay() {
  const ds = displaysLR(), L = settings.layout;
  if (L.display >= 0 && L.display < ds.length) return { d: ds[L.display], i: L.display };
  const p = screen.getPrimaryDisplay(), i = ds.findIndex(d => d.id === p.id);
  return { d: p, i: Math.max(0, i) };
}
// ---------- covering the taskbar ----------
// "focus": stretch to the bottom of the screen while Master Duel is the active window, back above the taskbar otherwise.
// "always": always full height. "never": always stop at the taskbar.
let mdFocused = false, fgProc = null, fgRestarts = 0;
const FG_SCRIPT = `
$sig = @'
using System; using System.Runtime.InteropServices;
public static class FG {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
'@
Add-Type -TypeDefinition $sig
$lastPid = -1
while ($true) {
  $h = [FG]::GetForegroundWindow(); [uint32]$p = 0; [void][FG]::GetWindowThreadProcessId($h, [ref]$p)
  if ($p -ne $lastPid) {
    $name = ''
    try { $name = (Get-Process -Id $p -ErrorAction Stop).ProcessName } catch {}
    [Console]::Out.WriteLine("$p|$name"); [Console]::Out.Flush(); $lastPid = $p
  }
  Start-Sleep -Milliseconds 600
}`;
function coverNow() {
  const c = settings.coverTaskbar || "focus";
  return c === "always" || (c === "focus" && mdFocused);
}
function startFgWatch() {
  if (process.platform !== "win32" || fgProc || (settings.coverTaskbar || "focus") !== "focus") return;
  try {
    const file = path.join(app.getPath("userData"), "fgwatch.ps1");
    fs.writeFileSync(file, FG_SCRIPT);
    fgProc = require("child_process").spawn("powershell.exe", ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", file], { windowsHide: true });
  } catch { fgProc = null; return; }
  let buf = "";
  fgProc.stdout.on("data", d => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1);
      const [pid, name] = line.split("|");
      if (!+pid || +pid === process.pid || /^(MasterDuelCompanion|electron|master-duel-companion)$/i.test(name || "")) continue; // clicking the overlay itself changes nothing
      const md = /master\s*duel|masterduel/i.test(name || "");
      if (md !== mdFocused) { mdFocused = md; applyLayout(); }
    }
  });
  fgProc.on("exit", () => {
    fgProc = null;
    if (mdFocused) { mdFocused = false; applyLayout(); }
    if (!quitting && fgRestarts++ < 5 && (settings.coverTaskbar || "focus") === "focus") setTimeout(startFgWatch, 3000);
  });
}
function stopFgWatch() { if (fgProc) { try { fgProc.kill(); } catch {} fgProc = null; } }
let quitting = false;
function setCover(mode) {
  settings.coverTaskbar = mode; saveSettings();
  if (mode === "focus") { fgRestarts = 0; startFgWatch(); } else stopFgWatch();
  applyLayout(); buildTrayMenu();
}

function layoutBounds() {
  const L = settings.layout, dsp = curDisplay().d, wa = coverNow() ? dsp.bounds : dsp.workArea;
  if (L.mode === "full") return { x: wa.x, y: wa.y, width: wa.width, height: wa.height };
  const w = Math.min(COMPACT_W, wa.width);
  return { x: L.side === "left" ? wa.x : wa.x + wa.width - w, y: wa.y, width: w, height: wa.height };
}
function applyLayout() {
  if (!win) return;
  win.setBounds(layoutBounds());
  if (process.env.OMNI_DEBUG) setTimeout(() => console.log("[layout]", settings.layout.mode, settings.layout.side, JSON.stringify(win.getBounds())), 300);
}
function setMode(mode) {
  mode = mode === "full" ? "full" : "compact";
  if (settings.layout.mode === mode && win) { applyLayout(); return; }
  settings.layout.mode = mode; saveSettings(); applyLayout();
}
function snap(side) {
  if (!win) return;
  const L = settings.layout, ds = displaysLR(), cur = curDisplay();
  if (L.mode === "full") { L.side = side; L.display = cur.i; saveSettings(); send("compact"); return; } // back to compact on that side
  let idx = cur.i * 2 + (L.side === "right" ? 1 : 0);
  idx += side === "left" ? -1 : 1;
  if (idx < 0 || idx >= ds.length * 2) return;
  L.display = Math.floor(idx / 2); L.side = idx % 2 ? "right" : "left";
  saveSettings(); applyLayout();
}

// ---------- hotkey editor ----------
let hkWin = null;
async function openHotkeys() {
  if (hkWin) { hkWin.show(); hkWin.focus(); return; }
  const th = win ? await win.webContents.executeJavaScript(`getComputedStyle(document.documentElement).getPropertyValue("--bg").trim()`).catch(() => "") : "";
  if (hkWin) return;
  const b = win ? win.getBounds() : { x: 100, y: 100, width: 540 };
  hkWin = new BrowserWindow({
    width: 520, height: 640, x: Math.max(0, b.x + Math.round((b.width - 520) / 2)), y: b.y + 40,
    frame: false, resizable: false, alwaysOnTop: true, backgroundColor: /^#[0-9a-f]{6}$/i.test(th) ? th : "#0A1630", title: "Overlay Hotkeys", icon: path.join(__dirname, "icon.ico"),
    webPreferences: { contextIsolation: true, nodeIntegration: false, preload: path.join(__dirname, "preload.js") }
  });
  hkWin.setAlwaysOnTop(true, "screen-saver");
  hkWin.loadFile(path.join(__dirname, "hotkeys.html"));
  hkWin.on("closed", () => { hkWin = null; registerHotkeys(); });
}
ipcMain.on("open-hotkeys", openHotkeys);
// The hotkey editor borrows the page's current theme colors.
ipcMain.handle("hk:theme", async () => {
  if (!win) return null;
  return win.webContents.executeJavaScript(`(() => { const c = getComputedStyle(document.documentElement), o = {};
    ["bg","panel","panel2","ink","muted","gold","red","line"].forEach(k => { const v = c.getPropertyValue("--" + k).trim(); if (v) o[k] = v; });
    return o; })()`).catch(() => null);
});
ipcMain.on("snap", (e, side) => snap(side === "left" ? "left" : "right"));
ipcMain.on("layout", (e, mode) => setMode(mode));
ipcMain.on("hk:close", () => { if (hkWin) hkWin.close(); });
ipcMain.on("hk:capture", (e, on) => { if (on) globalShortcut.unregisterAll(); else registerHotkeys(); }); // pause hotkeys while recording one
ipcMain.handle("hk:get", () => settings.hotkeys);
ipcMain.handle("hk:reset", () => { settings.hotkeys = Object.assign({}, DEFAULT_HOTKEYS); saveSettings(); registerHotkeys(); return settings.hotkeys; });
ipcMain.handle("hk:set", (e, { action, accel }) => {
  if (!DEFAULT_HOTKEYS[action]) return { ok: false, msg: "Unknown action." };
  const clash = Object.keys(settings.hotkeys).find(a => a !== action && settings.hotkeys[a] && settings.hotkeys[a].toLowerCase() === accel.toLowerCase());
  if (clash) return { ok: false, msg: "That combo is already used for another overlay action." };
  globalShortcut.unregisterAll();
  let ok = false;
  try { ok = globalShortcut.register(accel, () => {}); } catch { ok = false; }
  globalShortcut.unregisterAll();
  if (!ok) { registerHotkeys(); return { ok: false, msg: "Windows or another app already uses that combo. Try a different one." }; }
  settings.hotkeys[action] = accel; saveSettings(); registerHotkeys();
  return { ok: true, hotkeys: settings.hotkeys };
});

if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on("second-instance", () => { if (win) { win.show(); win.focus(); } });
  app.whenReady().then(() => {
    protocol.handle("app", (req) => {
      const rel = decodeURIComponent(new URL(req.url).pathname).replace(/^\/+/, "") || "index.html";
      const file = path.normalize(path.join(SITE_DIR, rel));
      if (!file.startsWith(SITE_DIR) || !fs.existsSync(file)) return new Response("Not found", { status: 404 });
      return net.fetch(pathToFileURL(file).toString());
    });
    createWindow();
    if (settings.reader) setTimeout(startReader, 2500);
    startFgWatch();
    setTimeout(() => checkUpdate(false), 6000);
    setInterval(() => checkUpdate(false), 4 * 60 * 60 * 1000);
    tray = new Tray(path.join(__dirname, "icon.ico"));
    tray.on("click", toggleVisible);
    registerHotkeys();
  });
  app.on("before-quit", () => { quitting = true; stopFgWatch(); });
  app.on("will-quit", () => { globalShortcut.unregisterAll(); stopFgWatch(); });
  app.on("window-all-closed", () => app.quit());
}
