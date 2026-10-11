"""The guide-maker check for one deck: each card's text next to what the engine reads from it (eval), to read by hand.
  python3 review.py <engine> <cards.cdb> <scripts.zip> <deck.json> [names to skip, ;-separated]"""
import json, sqlite3, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, dk = sys.argv[1:5]; skip = set(sys.argv[5].split(";")) if len(sys.argv) > 5 else set()
db = sqlite3.connect(cdb)
d = json.load(open(dk)); cards = list(dict.fromkeys(d["main"] + d["extra"]))
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
o = p.communicate(json.dumps({"id": 1, "cmd": "init", "cdb": cdb, "scripts": zipf}) + "\n" + json.dumps({"id": 2, "cmd": "eval", "cards": cards}) + "\n" + '{"cmd":"quit"}\n')[0]
ev = [json.loads(l) for l in o.splitlines() if json.loads(l).get("id") == 2][0]["eval"]
for c in cards:
    e = ev.get(str(c)); row = db.execute("select name, desc from texts where id=?", (c,)).fetchone()
    if not e or not row or row[0] in skip: continue
    vals = {k: round(v, 2) for k, v in e.items() if k in ("field", "set", "hand", "gy", "lock", "sturdy", "onSummon") and v}
    extra = {k: e[k] for k in ("revive", "fusion", "brings", "needs", "noScript", "oneUse") if k in e}
    print("==", row[0], vals, extra if extra else "")
    print("  ", row[1].replace("\r", " ").replace("\n", " ")[:650])
