// Worker thread for the LP reader. Tesseract switches to browser-style file loading when it sees Electron,
// which can't read the bundled language file, so hide Electron from it here and use the plain Node path.
try { delete process.versions.electron; } catch (e) {}
if (process.versions.electron) { try { Object.defineProperty(process.versions, "electron", { value: undefined }); } catch (e) {} }
require("tesseract.js/src/worker-script/node/index.js");
