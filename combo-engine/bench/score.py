"""Score end boards and show why: compare a guide's board with the engine's.
Usage: python3 score.py <engine> <cards.cdb> <scripts.zip> <boards.json>
boards.json: {"extra": [names or codes], "targets": [...], "boards": [{"name": "...", "field": [...], "zones": [...],
              "backrow": [...], "hand": [...], "gy": [...], "banished": [...]}]}
Cards can be names (exact, case-insensitive) or passcodes. "zones" gives each field card's zone (2 = center)."""
import json, sqlite3, subprocess, sys
exe, cdb, scripts, spec = sys.argv[1:5]
db = sqlite3.connect(cdb)
names = {}
for code, name in db.execute("select d.id, t.name from datas d join texts t on t.id = d.id order by (d.alias != 0), d.id"):
    names.setdefault(name.lower(), code)
def code(x):
    if isinstance(x, int) or str(x).isdigit(): return int(x)
    c = names.get(str(x).lower())
    if c is None: sys.exit("unknown card: %s" % x)
    return c
spec = json.load(open(spec, encoding="utf-8"))
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"]: return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": scripts})
extra = [code(x) for x in spec.get("extra", [])]
targets = [code(x) for x in spec.get("targets", [])]
for i, b in enumerate(spec["boards"]):
    q = {"id": 10 + i, "cmd": "score", "extra": extra, "targets": targets, "zones": b.get("zones", [])}
    for k in ("field", "backrow", "hand", "gy", "banished"): q[k] = [code(x) for x in b.get(k, [])]
    w = ask(q)["score"]
    print("\n== %s: %.2f" % (b.get("name", "board %d" % (i + 1)), w.get("total", 0)))
    for st in w.get("stops", []): print("   stop  %5.2f (counts %5.2f)  %s" % (st["value"], st["counts"], st["what"]))
    for fl in w.get("flat", []): print("   flat  %5.2f                %s" % (fl["value"], fl["what"]))
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
