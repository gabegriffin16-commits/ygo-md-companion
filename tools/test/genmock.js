(function(){
  var B="http://127.0.0.1:8813/";
  function post(u, b){ return fetch(B+u, {method:"POST", body:b===undefined?"":JSON.stringify(b)}).then(function(r){ return r.json(); }); }
  var oa = window.overlayApp = window.overlayApp || {};
  oa.genStart=function(j){ window.__genJob=j; return post("start", j); };
  oa.genStatus=function(){ return post("status"); };
  oa.genTake=function(id){ return post("take", {deckId:id}); };
  oa.genCancel=function(){ post("cancel"); };
  setInterval(function(){ post("events").then(function(a){ (a||[]).forEach(function(d){ window.dispatchEvent(new CustomEvent("overlay-gen",{detail:d})); }); }).catch(function(){}); }, 400);
})();
