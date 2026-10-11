// Generation quality: runs "Generate lines" (app/main.js, via the electron harness) on a fixed set of HERO hands and
// compares the best board it found for each hand with a long reference search (60 s each, cached).
//   OMNI_ENGINE=<engine> CE_SCRIPTS=<zip> CE_CDB=<cdb> node tools/test/genquality.js [refSecs]
// The first run computes the references (cached in genquality-ref.json); later runs measure generation.
const path = require("path"), fs = require("fs");
const h = require("./electron-harness.js");
const root = path.join(__dirname, "../..");
const d = JSON.parse(fs.readFileSync(path.join(root, "combo-engine/bench/omni.json"), "utf8"));
const S = { stratos: 40044918, faris: 18094166, vyon: 27780618, mist: 50720316, malicious: 9411399, increase: 22865492, ahl: 8949584, fdest: 52947044, call: 213326, poly: 24094653 };
const singles = [S.stratos, S.faris, S.vyon, S.mist, S.malicious, S.ahl, S.fdest, S.call].map(c => [c]);
const pairs = [[S.stratos, S.faris], [S.stratos, S.fdest], [S.faris, S.malicious], [S.stratos, S.vyon], [S.faris, S.vyon], [S.stratos, S.mist], [S.faris, S.ahl],
  [S.vyon, S.malicious], [S.ahl, S.call], [S.stratos, S.increase], [S.fdest, S.malicious], [S.call, S.faris]];
const hands = singles.concat(pairs);
const refSecs = Number(process.argv[2] || 60);
const refFile = path.join(root, "tools/test/genquality-ref.json");
(async () => {
  // reference: long unseeded searches (cached)
  let ref = {}; try { ref = JSON.parse(fs.readFileSync(refFile, "utf8")); } catch {}
  let computed = false;
  for (const hand of hands) {
    const k = hand.join(",");
    if (ref[k] !== undefined) continue;
    computed = true;
    const r = await h.handlers["combo:search"](null, { deck: d.main, extra: d.extra, hand: hand, timeMs: refSecs * 1000, top: 1 });
    ref[k] = r.boards && r.boards[0] ? r.boards[0].score : 0;
    fs.writeFileSync(refFile, JSON.stringify(ref, null, 1));
    console.log("ref", k, ref[k].toFixed(2));
  }
  // The reference searches fill the search memory, so generation is measured in a fresh process: run this again.
  if (computed) { console.log("references done; run again to measure generation"); process.exit(0); }
  const t0 = Date.now();
  await h.handlers["gen:start"](null, { deckId: "q", name: "q", deck: d.main, extra: d.extra, hands, sig: "x", targets: [] });
  while ((await h.handlers["gen:status"]()).running) await new Promise(r => setTimeout(r, 1000));
  const genSecs = (Date.now() - t0) / 1000;
  const lines = JSON.parse(fs.readFileSync(path.join(h.userData, "generated", "q.json"), "utf8")).lines;
  // what generation found per hand: a near-zero search returns the remembered best line
  let sum = 0, refSum = 0, worse = 0;
  for (const hand of hands) {
    const r = await h.handlers["combo:search"](null, { deck: d.main, extra: d.extra, hand, timeMs: 50, top: 1 });
    const got = r.boards && r.boards[0] ? r.boards[0].score : 0, want = ref[hand.join(",")];
    sum += got; refSum += want; if (got < want - 0.05) worse++;
    console.log("%s  generation %s  reference %s%s", hand.join("+").padEnd(18), got.toFixed(2), want.toFixed(2), got < want - 0.05 ? "  <- below" : got > want + 0.05 ? "  (better)" : "");
  }
  console.log("\ngeneration took %ss, %d lines; total %s vs reference %s (%s%%); %d of %d hands below the reference", genSecs.toFixed(0), lines.length, sum.toFixed(2), refSum.toFixed(2), (100 * sum / refSum).toFixed(1), worse, hands.length);
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
