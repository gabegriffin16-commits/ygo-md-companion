// HTTP bridge for page tests: tools/test/genmock.js talks to this (port 8813), which drives app/main.js's
// "Generate lines" through the electron harness. Same env vars as electron-harness.js.
const h = require("./electron-harness.js"), http = require("http");
let seen = 0;
http.createServer(async (req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*"); res.setHeader("Access-Control-Allow-Headers", "*");
  if (req.method === "OPTIONS") { res.end(); return; }
  let body = ""; for await (const c of req) body += c; const q = body ? JSON.parse(body) : null;
  let out = null;
  if (req.url === "/start") out = await h.handlers["gen:start"](null, q);
  else if (req.url === "/status") out = await h.handlers["gen:status"]();
  else if (req.url === "/take") out = await h.handlers["gen:take"](null, q.deckId);
  else if (req.url === "/events") { out = h.events.slice(seen).filter(e => e.name === "overlay-gen").map(e => e.detail); seen = h.events.length; }
  else if (req.url === "/cancel") { h.ons["gen:cancel"](); out = {}; }
  res.setHeader("Content-Type", "application/json"); res.end(JSON.stringify(out));
}).listen(8813, "127.0.0.1", () => console.log("gen bridge up"));
