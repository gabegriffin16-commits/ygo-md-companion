// Minimal in-browser stand-in for supabase-js, with the same row rules as schema.sql.
(function(){
  window.MDC_CLOUD = {url:"https://mock.supabase.co", key:"anon"};
  function db(){ try { return JSON.parse(localStorage.getItem("mockdb")) || {users:[], profiles:[], decks:[], session:null}; } catch(e){ return {users:[], profiles:[], decks:[], session:null}; } }
  function save(d){ localStorage.setItem("mockdb", JSON.stringify(d)); }
  function uid(){ var d = db(); return d.session && d.session.user.id; }
  function isAdmin(){ var d = db(), u = uid(); var p = d.profiles.filter(function(x){ return x.id === u; })[0]; return !!(p && p.is_admin); }
  function visible(t, row){ var u = uid(); if (!u) return false; if (t === "profiles" || t.indexOf("bench") === 0) return true; return row.owner === u || row.shared || isAdmin(); }
  function Q(t){ this.t = t; this.f = []; this.op = "select"; this.ord = null; this.single = false; }
  Q.prototype.select = function(){ return this; };
  Q.prototype.eq = function(c, v){ this.f.push([c, v]); return this; };
  Q.prototype.order = function(c, o){ this.ord = [c, !(o && o.ascending === false)]; return this; };
  Q.prototype.maybeSingle = function(){ this.single = true; return this; };
  Q.prototype.upsert = function(row){ this.op = "upsert"; this.row = row; return this; };
  Q.prototype.limit = function(){ return this; };
  Q.prototype.insert = function(row){ this.op = "insert"; this.row = row; return this; };
  Q.prototype.delete = function(){ this.op = "delete"; return this; };
  Q.prototype.then = function(ok, no){
    var d = db(), t = this.t, self = this, u = uid(), res;
    var match = function(r){ return self.f.every(function(f){ return r[f[0]] === f[1]; }); };
    if (this.op === "select"){
      var rows = (d[t] || []).filter(function(r){ return visible(t, r) && match(r); });
      if (this.ord){ var c = this.ord[0], asc = this.ord[1]; rows.sort(function(a, b){ return (a[c] > b[c] ? 1 : a[c] < b[c] ? -1 : 0) * (asc ? 1 : -1); }); }
      res = {data: this.single ? (rows[0] || null) : JSON.parse(JSON.stringify(rows)), error:null};
    } else if (this.op === "insert"){
      d[t] = d[t] || []; var row = Object.assign({id:d[t].length + 1}, this.row); d[t].push(row); save(d); res = {data:null, error:null};
    } else if (this.op === "upsert"){
      if (this.row.owner !== u) res = {data:null, error:{message:"new row violates row-level security policy"}};
      else { var i = d.decks.findIndex(function(r){ return r.id === self.row.id; });
        if (i >= 0 && d.decks[i].owner !== u) res = {data:null, error:{message:"row-level security"}};
        else { if (i >= 0) d.decks[i] = this.row; else d.decks.push(this.row); save(d); res = {data:null, error:null}; } }
    } else {
      d.decks = d.decks.filter(function(r){ return !(match(r) && r.owner === u); }); save(d); res = {data:null, error:null};
    }
    return Promise.resolve(res).then(ok, no);
  };
  var auth = {
    getSession: function(){ return Promise.resolve({data:{session:db().session}}); },
    signInWithPassword: function(o){ var d = db(); var u = d.users.filter(function(x){ return x.email === o.email && x.pw === o.password; })[0];
      if (!u) return Promise.resolve({data:{}, error:{message:"Invalid login credentials"}});
      d.session = {user:{id:u.id, user_metadata:u.meta}}; save(d); return Promise.resolve({data:{user:d.session.user, session:d.session}, error:null}); },
    signUp: function(o){ var d = db(); if (d.users.some(function(x){ return x.email === o.email; })) return Promise.resolve({data:{}, error:{message:"User already registered"}});
      var id = "u" + Math.random().toString(36).slice(2, 8); d.users.push({id:id, email:o.email, pw:o.password, meta:o.options.data});
      d.profiles.push({id:id, username:o.options.data.username, is_admin:false, created_at:new Date().toISOString()});
      d.session = {user:{id:id, user_metadata:o.options.data}}; save(d); return Promise.resolve({data:{user:d.session.user, session:d.session}, error:null}); },
    signOut: function(){ var d = db(); d.session = null; save(d); return Promise.resolve({}); }
  };
  window.supabase = {createClient: function(){ return {auth:auth, from:function(t){ return new Q(t); }}; }};
  window.mockAdmin = function(name){ var d = db(); d.profiles.forEach(function(p){ if (p.username.toLowerCase() === name.toLowerCase()) p.is_admin = true; }); save(d); };
})();
