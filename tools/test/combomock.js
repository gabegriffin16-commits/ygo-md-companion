(function(){
  function ext(){
    var oa = window.overlayApp = window.overlayApp || {};
    oa.comboAvailable = function(){ return Promise.resolve(true); };
    oa.stopCombos = function(){ window.__stopped = true; };
    oa.findCombos = function(q){
      window.__q = q; q = Object.assign({}, q, {timeMs: window.__cfMs || 4000});
      var ev = function(d){ window.dispatchEvent(new CustomEvent("overlay-combo", {detail:d})); };
      ev({id:5, stage:"prepare"});
      ev({id:5, stage:"download", what:"card scripts", got:5*1048576, total:0});
      var t0 = Date.now(), iv = setInterval(function(){ ev({id:5, progress:{replays:Math.round((Date.now()-t0)/4), boards:12, seconds:(Date.now()-t0)/1000}}); }, 500);
      return fetch("http://127.0.0.1:8812/", {method:"POST", body:JSON.stringify(q)}).then(function(r){ return r.json(); }).then(function(o){
        clearInterval(iv); (o.events || []).forEach(function(e){ if (e.warning) ev(e); }); return o; });
    };
  }
  ext();
})();
