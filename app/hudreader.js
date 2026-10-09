// HUD reader: reads Master Duel's turn/phase indicator and both LP numbers from a frame of the game.
// Everything is measured on a 1920x1080 frame and scaled to whatever size the capture really is.
// The turn indicator is the round badge on the right of the field: blue on your turn, red on theirs,
// with "Turn N" and the phase name in white. LP numbers are read by the app (OCR) from the crops made here.
(function (root) {
  "use strict";

  function decodeBits(t) {
    var b = new Uint8Array(t.w * t.h);
    for (var i = 0; i < b.length; i++) b[i] = t.bits.charCodeAt(i) === 49 ? 1 : 0;
    return { w: t.w, h: t.h, b: b, n: b.reduce(function (a, v) { return a + v; }, 0) };
  }

  // White text on a blue or red badge: bright and not strongly colored.
  function textMask(img, x0, y0, w, h, W) {
    var d = img.data, out = new Uint8Array(w * h);
    for (var y = 0; y < h; y++) for (var x = 0; x < w; x++) {
      var i = ((y0 + y) * W + (x0 + x)) * 4, r = d[i], g = d[i + 1], b = d[i + 2];
      var mn = Math.min(r, g, b);
      out[y * w + x] = (r + g + b) / 3 > 150 && mn > 105 ? 1 : 0;
    }
    return out;
  }

  // Best overlap (Dice score) of a template inside a region, allowing a few pixels of drift.
  function bestScore(mask, mw, mh, T, slack) {
    var best = 0;
    for (var dy = 0; dy <= slack * 2; dy++) for (var dx = 0; dx <= slack * 2; dx++) {
      if (dx + T.w > mw || dy + T.h > mh) continue;
      var both = 0, on = 0;
      for (var y = 0; y < T.h; y++) {
        var row = (dy + y) * mw + dx, trow = y * T.w;
        for (var x = 0; x < T.w; x++) { var m = mask[row + x]; on += m; if (m && T.b[trow + x]) both++; }
      }
      var s = on + T.n ? 2 * both / (on + T.n) : 0;
      if (s > best) best = s;
    }
    return best;
  }

  function HudReader(spec) {
    this.spec = spec;
    this.t = {};
    for (var k in spec.t) this.t[k] = spec.t[k].map(decodeBits);
    this.canvas = document.createElement("canvas");
    this.ctx = this.canvas.getContext("2d", { willReadFrequently: true });
  }
  var SLACK = 5;

  // Draw a reference-space rectangle from the source into a canvas at reference scale (plus slack).
  HudReader.prototype.grab = function (src, sw, sh, R, pad) {
    pad = pad || 0;
    var sx = sw / 1920, sy = sh / 1080, w = R[2] + pad * 2, h = R[3] + pad * 2;
    this.canvas.width = w; this.canvas.height = h;
    this.ctx.drawImage(src, (R[0] - pad) * sx, (R[1] - pad) * sy, w * sx, h * sy, 0, 0, w, h);
    return this.ctx.getImageData(0, 0, w, h);
  };

  HudReader.prototype.score = function (img, key) {
    var m = textMask(img, 0, 0, img.width, img.height, img.width), self = this;
    return Math.max.apply(null, this.t[key].map(function (T) { return bestScore(m, img.width, img.height, T, SLACK); }));
  };

  // Returns {visible, mine, phase, turn1, s:{...scores}}. phase is draw / standby / main / other.
  HudReader.prototype.readTurn = function (src, sw, sh) {
    var S = this.spec;
    var turnImg = this.grab(src, sw, sh, S.turn, SLACK);
    var turn = this.score(turnImg, "turn");
    if (turn < 0.75) return { visible: false, s: { turn: turn } };
    // Badge color: sample the middle of the badge.
    var core = this.grab(src, sw, sh, [S.box[0] + 40, S.box[1] + 30, 100, 45], 0), d = core.data, r = 0, b = 0;
    for (var i = 0; i < d.length; i += 4) { r += d[i]; b += d[i + 2]; }
    var px = d.length / 4; r /= px; b /= px;
    var mine = b > r + 20 ? true : r > b + 20 ? false : null;
    if (mine === null) return { visible: false, s: { turn: turn } };
    var ph = this.grab(src, sw, sh, S.phase, SLACK), sc = {}, best = "other", bs = 0;
    ["draw", "standby", "main"].forEach(function (k) { sc[k] = this.score(ph, k); if (sc[k] > bs) { bs = sc[k]; best = k; } }, this);
    if (bs < 0.7) best = "other";       // Battle, Main 2, End, or mid-animation
    var dig = this.grab(src, sw, sh, S.digit, SLACK), d1 = this.score(dig, "d1"), d2 = this.score(dig, "d2");
    return { visible: true, mine: mine, phase: best, turn1: d1 > 0.7 && d1 > d2 + 0.1, s: { turn: turn, phase: bs, d1: d1, d2: d2 } };
  };

  // LP: white digits. Returns {sig, canvas} where canvas holds black-on-white digits ready for OCR.
  var LP_RECTS = { me: [220, 995, 180, 55], opp: [1600, 60, 170, 55] };
  HudReader.prototype.lpCrop = function (src, sw, sh, who) {
    var img = this.grab(src, sw, sh, LP_RECTS[who], 0), d = img.data, w = img.width, h = img.height;
    var c = document.createElement("canvas"); c.width = w * 2; c.height = h * 2;
    var cx = c.getContext("2d"), out = cx.createImageData(w, h), on = 0, sig = [];
    for (var i = 0; i < w * h; i++) {
      var r = d[i * 4], g = d[i * 4 + 1], b = d[i * 4 + 2], mn = Math.min(r, g, b), mx = Math.max(r, g, b);
      var white = mn > 170 && mx - mn < 60, v = white ? 0 : 255;
      out.data[i * 4] = out.data[i * 4 + 1] = out.data[i * 4 + 2] = v; out.data[i * 4 + 3] = 255;
      if (white) on++;
    }
    // A coarse signature (8x4 grid of ink counts) to tell when the number changed without running OCR.
    for (var gy = 0; gy < 4; gy++) for (var gx = 0; gx < 8; gx++) {
      var cnt = 0;
      for (var y = Math.floor(gy * h / 4); y < Math.floor((gy + 1) * h / 4); y++)
        for (var x = Math.floor(gx * w / 8); x < Math.floor((gx + 1) * w / 8); x++) if (out.data[(y * w + x) * 4] === 0) cnt++;
      sig.push(Math.round(cnt / 12));
    }
    var tmp = document.createElement("canvas"); tmp.width = w; tmp.height = h; tmp.getContext("2d").putImageData(out, 0, 0);
    cx.imageSmoothingEnabled = true; cx.drawImage(tmp, 0, 0, w * 2, h * 2);
    return { sig: sig.join(","), ink: on, canvas: c };
  };

  // Only report a turn/phase once it has read the same twice in a row.
  function HudTracker() { this.last = null; this.cand = null; this.n = 0; this.sent = null; }
  HudTracker.prototype.push = function (r) {
    if (!r || !r.visible) { this.cand = null; this.n = 0; return null; }
    var key = (r.mine ? "me" : "opp") + ":" + r.phase + ":" + (r.turn1 ? 1 : 0);
    if (key === this.cand) this.n++; else { this.cand = key; this.n = 1; }
    if (this.n < 2 || key === this.sent) return null;
    this.sent = key;
    return { mine: r.mine, phase: r.phase, turn1: r.turn1 };
  };

  // LP: OCR only when the digits look different and have held still for two reads (they animate when LP changes).
  function LpWatch() { this.stable = {}; this.pend = {}; }
  LpWatch.prototype.check = function (who, crop) {
    if (crop.ink < 150) { this.pend[who] = null; return false; }      // not on screen right now
    var p = this.pend[who];
    if (p && p.sig === crop.sig) { p.n++; } else { this.pend[who] = p = { sig: crop.sig, n: 1 }; }
    if (p.n === 2 && this.stable[who] !== crop.sig) { this.stable[who] = crop.sig; return true; }
    return false;
  };

  root.OmniHud = { HudReader: HudReader, HudTracker: HudTracker, LpWatch: LpWatch };
})(typeof window !== "undefined" ? window : this);
