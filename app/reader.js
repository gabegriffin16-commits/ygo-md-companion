// Hidden window: watches the Master Duel window and reports your hand to the overlay.
(function () {
  "use strict";
  var video = document.getElementById("v"), canvas = document.getElementById("c");
  var ctx = canvas.getContext("2d", { willReadFrequently: true });
  var reader = null, tracker = new OmniHand.HandTracker(), stream = null, blackReads = 0, skipWindow = false;
  var INTERVAL = 900;
  // Turn/phase badge and LP numbers (see hudreader.js).
  var hud = null, hudTrack = new OmniHud.HudTracker(), lpWatch = new OmniHud.LpWatch(), lpBusy = {};
  function readHud(src, w, h) {
    if (!hud) return;
    try {
      var r = hud.readTurn(src, w, h), ev = hudTrack.push(r);
      if (ev) readerApp.turn(ev);
      ["me", "opp"].forEach(function (who) {
        var c = hud.lpCrop(src, w, h, who);
        if (lpWatch.check(who, c) && !lpBusy[who]) {
          lpBusy[who] = true;
          readerApp.lp(who, c.canvas.toDataURL("image/png")).then(function (v) {
            lpBusy[who] = false;
            if (v == null) lpWatch.stable[who] = null;           // unreadable: try again on the next frame
          }, function () { lpBusy[who] = false; lpWatch.stable[who] = null; });
        }
      });
    } catch (e) { console.log("hud error " + e.message); }
  }

  function cvReady() {
    // opencv.js may finish loading before or after we ask, and may hand back a promise: cover all three.
    return new Promise(function (resolve) {
      var c = window.cv;
      // (The module object is itself "thenable", so strip .then before resolving or the promise never settles.)
      if (c && typeof c.then === "function") { c.then(function (m) { delete m.then; window.cv = m; resolve(m); }); return; }
      if (c && c.Mat) { resolve(c); return; }
      if (c) c.onRuntimeInitialized = function () { resolve(window.cv); };
      var iv = setInterval(function () { if (window.cv && window.cv.Mat) { clearInterval(iv); resolve(window.cv); } }, 200);
    });
  }
  function decode(bytes) {
    return createImageBitmap(new Blob([bytes], { type: "image/jpeg" })).then(function (bmp) {
      var cv2 = document.createElement("canvas"); cv2.width = bmp.width; cv2.height = bmp.height;
      var x = cv2.getContext("2d"); x.drawImage(bmp, 0, 0);
      return x.getImageData(0, 0, bmp.width, bmp.height);
    });
  }
  function status(state, extra) { readerApp.status(Object.assign({ state: state }, extra || {})); }
  function wait(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }

  async function loadTemplates() {
    for (;;) {
      var list = await readerApp.templates();
      if (list && list.length) {
        reader = new OmniHand.HandReader(window.cv);
        for (var i = 0; i < list.length; i++) {
          try { reader.addTemplate(list[i].key, list[i].name, await decode(list[i].bytes)); } catch (e) {}
        }
        return reader.templates.length;
      }
      status("starting", { note: "Waiting for the companion page" });
      await wait(3000);
    }
  }

  async function openSource(cfg) {
    if (cfg.testVideo) {
      video.src = cfg.testVideo; video.playbackRate = cfg.rate || 1; await video.play();
      return { name: "test video" };
    }
    var src = await readerApp.source(skipWindow);
    if (!src) return null;
    stream = await navigator.mediaDevices.getUserMedia({ audio: false, video: { mandatory: {
      chromeMediaSource: "desktop", chromeMediaSourceId: src.id, maxWidth: 1920, maxHeight: 1080, maxFrameRate: 4 } } });
    stream.getVideoTracks()[0].addEventListener("ended", function () { stream = null; });
    video.srcObject = stream; await video.play();
    return src;
  }

  function isBlack(img) {
    var d = img.data, sum = 0, n = 0;
    for (var i = 0; i < d.length; i += 4 * 97) { sum += d[i] + d[i + 1] + d[i + 2]; n++; }
    return sum / (n * 3) < 4;
  }

  async function main() {
    status("starting");
    var cfg = await readerApp.config();
    console.log("config ok", JSON.stringify(cfg));
    window.cv = await cvReady();
    console.log("opencv ready");
    try { hud = new OmniHud.HudReader(await readerApp.hudSpec()); } catch (e) { hud = null; console.log("no hud " + e.message); }
    var n = await loadTemplates();
    console.log("templates", n);
    status("searching", { cards: n });
    var src = null;
    for (;;) {
      if (!src || (!cfg.testVideo && !stream)) {
        src = null; tracker = new OmniHand.HandTracker(); hudTrack = new OmniHud.HudTracker(); lpWatch = new OmniHud.LpWatch();
        try { src = await openSource(cfg); } catch (e) { src = null; }
        if (!src) { status("searching"); await wait(4000); continue; }
        status("reading", { source: src.name });
      }
      if (cfg.testVideo && video.ended) { status("done"); return; }
      var t0 = performance.now();
      if (video.videoWidth) {
        var r = OmniHand.HandReader.stripRect(video.videoWidth, video.videoHeight);
        canvas.width = r.outW; canvas.height = r.outH;
        ctx.drawImage(video, r.x, r.y, r.w, r.h, 0, 0, r.outW, r.outH);
        var img = ctx.getImageData(0, 0, r.outW, r.outH);
        if (isBlack(img)) {
          // Exclusive fullscreen can hand back black frames from the game window: fall back to the screen.
          if (++blackReads >= 6 && !skipWindow && !cfg.testVideo) {
            skipWindow = true; blackReads = 0;
            if (stream) stream.getTracks().forEach(function (t) { t.stop(); });
            stream = null; src = null; continue;
          }
        } else blackReads = 0;
        readHud(video, video.videoWidth, video.videoHeight);
        var list = reader.read(img);
        var ev = tracker.push(list);
        if (ev) readerApp.hand(ev);
        status("reading", { source: src.name, seen: list.length, ms: Math.round(performance.now() - t0) });
      }
      await wait(Math.max(150, INTERVAL - (performance.now() - t0)));
    }
  }
  main().catch(function (e) { status("error", { note: String(e && e.message || e) }); });
})();
