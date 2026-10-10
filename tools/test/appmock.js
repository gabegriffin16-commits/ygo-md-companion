(function(){
  var st = {opacity:0.95, reader:true, readerLabel:"waiting for Master Duel", clickThrough:false, lockKey:"Ctrl+Shift+L", version:"1.11.0"};
  window.__set = [];
  function push(){ window.dispatchEvent(new CustomEvent("overlay-settings", {detail:JSON.parse(JSON.stringify(st))})); }
  window.overlayApp = {snap:function(){}, layout:function(m){ window.__layout = m; }, openHotkeys:function(){ window.__hk = (window.__hk||0)+1; },
    installUpdate:function(){}, getSettings:function(){ return Promise.resolve(JSON.parse(JSON.stringify(st))); },
    setSetting:function(k, v){ window.__set.push([k, v]); st[k] = v; if (k === "reader") st.readerLabel = v ? "reading" : "off"; setTimeout(push, 10); }};
})();
