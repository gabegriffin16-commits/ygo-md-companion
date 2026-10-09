// Hand reader: recognizes the cards in your Master Duel hand from a screenshot of the game.
// It matches AKAZE features from each deck card's art against the strip of screen where your hand sits.
// Runs in the overlay's hidden reader window (browser) and in Node for testing.
(function (root) {
  "use strict";

  // Where the hand sits on a 1920x1080 Master Duel screen, as fractions so other resolutions scale.
  var REGION = { x0: 200 / 1920, x1: 1720 / 1920, y0: 860 / 1080, y1: 1 };
  var WORK_H = 330;          // the hand strip is scaled to this height before matching (1.5x at 1080p)
  var ART_W = 205;           // card art is scaled to this width for the templates (matches WORK_H)
  var MIN_MATCHES = 18;      // a real card scores 50-150, a wrong guess under 5
  var CLUSTER_GAP = 0.034;   // matches further apart than this (fraction of strip width) are different cards

  function HandReader(cv, opt) {
    opt = opt || {};
    this.cv = cv;
    this.det = new cv.AKAZE();
    this.det.setThreshold(opt.threshold || 0.0005);
    this.bf = new cv.BFMatcher(cv.NORM_HAMMING, false);
    this.templates = [];
    this.minMatches = opt.minMatches || MIN_MATCHES;
  }

  // The rectangle (in video pixels) to grab, and the size to draw it at.
  HandReader.stripRect = function (vw, vh) {
    var x = Math.round(REGION.x0 * vw), y = Math.round(REGION.y0 * vh);
    var w = Math.round((REGION.x1 - REGION.x0) * vw), h = Math.round((REGION.y1 - REGION.y0) * vh);
    var s = WORK_H / h;
    return { x: x, y: y, w: w, h: h, outW: Math.round(w * s), outH: WORK_H };
  };

  // imageData: the full official card image (RGBA). key: the deck id ("stratos").
  HandReader.prototype.addTemplate = function (key, name, imageData) {
    var cv = this.cv;
    var m = cv.matFromImageData(imageData), g = new cv.Mat();
    cv.cvtColor(m, g, cv.COLOR_RGBA2GRAY); m.delete();
    var r = new cv.Rect(Math.round(g.cols * 0.08), Math.round(g.rows * 0.17), Math.round(g.cols * 0.84), Math.round(g.rows * 0.55));
    var art = g.roi(r), s = new cv.Mat(), f = ART_W / r.width;
    cv.resize(art, s, new cv.Size(0, 0), f, f, cv.INTER_AREA);
    var kp = new cv.KeyPointVector(), de = new cv.Mat(), mask = new cv.Mat();
    this.det.detectAndCompute(s, mask, kp, de);
    this.templates.push({ key: key, name: name, de: de, n: kp.size() });
    kp.delete(); mask.delete(); s.delete(); art.delete(); g.delete();
  };

  // stripData: the hand strip already scaled to stripRect().outW x outH (RGBA ImageData).
  // Returns the cards found, left to right: [{key, name, x (0..1), score}]
  HandReader.prototype.read = function (stripData) {
    var cv = this.cv;
    var m = cv.matFromImageData(stripData), g = new cv.Mat();
    cv.cvtColor(m, g, cv.COLOR_RGBA2GRAY); m.delete();
    var kp = new cv.KeyPointVector(), de = new cv.Mat(), mask = new cv.Mat();
    this.det.detectAndCompute(g, mask, kp, de);
    var W = g.cols, gap = CLUSTER_GAP * W, found = [];
    if (de.rows >= 2) {
      for (var t = 0; t < this.templates.length; t++) {
        var T = this.templates[t];
        if (T.de.rows < 2) continue;
        var ms = new cv.DMatchVectorVector();
        this.bf.knnMatch(T.de, de, ms, 2);
        var xs = [];
        for (var i = 0; i < ms.size(); i++) {
          var p = ms.get(i);
          if (p.size() >= 2) {
            var a = p.get(0), b = p.get(1);
            if (a.distance < 0.8 * b.distance) xs.push(kp.get(a.trainIdx).pt.x);
          }
          p.delete();
        }
        ms.delete();
        xs.sort(function (a, b) { return a - b; });
        var cl = [];
        for (var j = 0; j < xs.length; j++) {
          var last = cl[cl.length - 1];
          if (last && xs[j] - last[last.length - 1] < gap) last.push(xs[j]); else cl.push([xs[j]]);
        }
        for (var c = 0; c < cl.length; c++) {
          if (cl[c].length >= this.minMatches) found.push({ key: T.key, name: T.name, x: cl[c][cl[c].length >> 1] / W, score: cl[c].length });
        }
      }
    }
    kp.delete(); de.delete(); mask.delete(); g.delete();
    // Two different cards can't sit in the same spot: keep the stronger one.
    found.sort(function (a, b) { return b.score - a.score; });
    var keep = [];
    found.forEach(function (f) { if (!keep.some(function (k) { return Math.abs(k.x - f.x) < 0.04; })) keep.push(f); });
    return keep.sort(function (a, b) { return a.x - b.x; });
  };

  // Turns noisy reads into a steady hand and reports what was added.
  // Pop-ups and animations hide cards for a moment, so a card that vanishes and comes back
  // within RESTORE_MS isn't counted as a new card.
  function HandTracker(opt) {
    opt = opt || {};
    this.need = opt.need || 2;            // identical reads in a row before a hand counts
    this.needEmpty = opt.needEmpty || 5;  // an empty hand has to hold longer (pop-ups hide everything)
    this.restoreMs = opt.restoreMs || 15000;
    this.lastKey = null; this.same = 0; this.stableKey = null;
    this.known = {}; this.gone = [];      // gone: [{key, t}] cards that recently left the hand
  }
  function counts(list) { var o = {}; list.forEach(function (c) { o[c.key] = (o[c.key] || 0) + 1; }); return o; }
  function total(o) { var n = 0; for (var k in o) n += o[k]; return n; }
  HandTracker.prototype.push = function (list, now) {
    now = now || Date.now();
    var key = list.map(function (c) { return c.key; }).sort().join(",");
    if (key === this.lastKey) this.same++; else { this.lastKey = key; this.same = 1; }
    if (this.same < (key ? this.need : this.needEmpty) || key === this.stableKey) return null;
    this.stableKey = key;
    var R = counts(list), K = this.known, added = [], removed = [], k, i;
    this.gone = this.gone.filter(function (g) { return now - g.t < this.restoreMs; }, this);
    for (k in K) for (i = 0; i < K[k] - (R[k] || 0); i++) { removed.push(k); this.gone.push({ key: k, t: now }); }
    for (k in R) for (i = 0; i < R[k] - (K[k] || 0); i++) {
      var gi = -1;
      for (var j = 0; j < this.gone.length; j++) if (this.gone[j].key === k) { gi = j; break; }
      if (gi >= 0) this.gone.splice(gi, 1);   // it was only hidden for a moment
      else added.push(k);
    }
    this.known = R;
    return { hand: R, size: total(R), added: added, removed: removed, order: list.map(function (c) { return c.key; }) };
  };

  var api = { HandReader: HandReader, HandTracker: HandTracker, REGION: REGION };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.OmniHand = api;
})(this);
