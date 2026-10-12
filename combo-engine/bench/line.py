"""Search one hand and print the best line step by step, with the end board (for reading lines like a guide).
  python3 line.py <engine> <cards.cdb> <scripts.zip> <deck.json> <secs> "<card name>;<card name>..." ["<goal name>;..."]
Env: THREADS (default 12), ACTS (max actions, 40), TOP (boards to print, 1), MODE (beam / dfs), LABELS=<file> (save the best line's labels)."""
import json, os, sqlite3, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, dk, secs, hand_s = sys.argv[1:7]; goal_s = sys.argv[7] if len(sys.argv) > 7 else ""
db = sqlite3.connect(cdb)
names = {}
for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id=d.id order by (d.alias != 0), d.id"): names.setdefault(nm.lower(), c)
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0] if c else "(draw)"
d = json.load(open(dk)); deck = list(d["main"])
hand = [names[x.strip().lower()] for x in hand_s.split(";") if x.strip()]
goals = [names[x.strip().lower()] for x in goal_s.split(";") if x.strip()]
for c in hand:
    if c in deck: deck.remove(c)
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready")): return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": zipf})
r = ask({"id": 2, "cmd": "search", "deck": deck, "extra": d["extra"], "hand": hand, "timeMs": int(float(secs) * 1000), "threads": int(os.environ.get("THREADS") or 12),
         "top": int(os.environ.get("TOP") or 1), "maxActions": int(os.environ.get("ACTS") or 40), "targets": goals, "labels": True,
         **({"mode": os.environ["MODE"]} if os.environ.get("MODE") else {})})
print("hand:", ", ".join(map(nm, hand)), "| best found %.0fs of %ss%s" % (r["stats"]["bestAt"], secs, ", every line checked" if r.get("complete") else ""))
for b in r.get("boards", []):
    print("\nSCORE %.2f" % b["score"])
    for i, s in enumerate(b["steps"], 1):
        picks = ", ".join(nm(c) for c in s.get("picks", []))
        print("  %2d. %s %s%s%s" % (i, s["do"], nm(s["card"]), (": " + s["effect"][:70]) if s.get("effect") else "", ("  [" + picks + "]") if picks else ""))
    for k, lbl in (("field", "FIELD"), ("backrow", "BACKROW"), ("hand", "HAND"), ("gy", "GY"), ("banished", "BANISHED")):
        if b.get(k): print("  %-9s %s" % (lbl + ":", ", ".join(map(nm, b[k]))))
if os.environ.get("LABELS") and r.get("boards"): json.dump(r["boards"][0].get("labels", []), open(os.environ["LABELS"], "w"))
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
