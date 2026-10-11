"""Whole-database check for text/script reading changes: runs "eval" on every card with two engine builds and prints
each card whose reading changed (field / set / hand / GY / onSummon / lock / sturdy / brings / needs). Read every changed
card's text before keeping a rule; a few seconds per build.
  python3 evaldiff.py <old engine> <new engine> <cards.cdb> <scripts.zip> [deck.json ...]
With deck files: only their cards (e.g. the five test decks, which should usually not change)."""
import json, sqlite3, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8")
old, new, cdb, zipf = sys.argv[1:5]; decks = sys.argv[5:]
db = sqlite3.connect(cdb)
if decks:
    ids = sorted({c for f in decks for k in ("main", "extra") for c in json.load(open(f))[k]})
else:
    ids = [r[0] for r in db.execute("select d.id from datas d join texts t on t.id=d.id where d.alias=0 and d.ot & 3 and d.type & 0x4000 = 0 and d.id < 100000000")]
def ev(exe):
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
    o = p.communicate(json.dumps({"id": 1, "cmd": "init", "cdb": cdb, "scripts": zipf}) + "\n" + json.dumps({"id": 2, "cmd": "eval", "cards": ids}) + "\n" + '{"cmd":"quit"}\n')[0]
    return [json.loads(l) for l in o.splitlines() if json.loads(l).get("id") == 2][0]["eval"]
a, b = ev(old), ev(new)
keys = ["field", "set", "hand", "gy", "onSummon", "lock", "sturdy", "brings", "needs"]
r = lambda v: round(v, 2) if isinstance(v, float) else v
n = 0
for k in sorted(b, key=lambda k: b[k]["name"]):
    x, y = a.get(k, {}), b[k]
    ch = {t: (r(x.get(t)), r(y.get(t))) for t in keys if r(x.get(t)) != r(y.get(t))}
    if ch: n += 1; print("%-42s %s" % (y["name"][:42], ch))
print("%d changed of %d" % (n, len(b)))
