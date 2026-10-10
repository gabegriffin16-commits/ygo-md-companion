"""Deep-search benchmark: runs one hand through combo-engine with a big time and depth budget and prints the best boards.
Usage: python3 bench.py <engine> <cards.cdb> <scripts.zip> <hand codes comma-separated> <seconds> <maxActions> [threads] [mode] [targets]"""
import json, os, sqlite3, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__)); from deck import MAIN, EXTRA
if os.environ.get("DECK_JSON"):   # {"main":[codes],"extra":[codes]}
    _d = json.load(open(os.environ["DECK_JSON"])); MAIN, EXTRA = _d["main"], _d["extra"]
exe, cdb, scripts, hand, secs, acts = sys.argv[1:7]
threads = int(sys.argv[7]) if len(sys.argv) > 7 else os.cpu_count()
mode = sys.argv[8] if len(sys.argv) > 8 else ""
targets = [int(x) for x in sys.argv[9].split(",")] if len(sys.argv) > 9 and sys.argv[9] else []
hand = [int(x) for x in hand.split(",")]
db = sqlite3.connect(cdb)
def n(c):
    r = db.execute("select name from texts where id=?", (c,)).fetchone(); return r[0] if r else str(c)
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def send(o): p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
last = [0]
def wait(i):
    while True:
        o = json.loads(p.stdout.readline())
        if o.get("progress") and o["progress"]["seconds"] - last[0] >= 60: last[0] = o["progress"]["seconds"]; print("  ...", o["progress"], flush=True)
        if o.get("id") == i and (o.get("done") or o.get("error") or o.get("ready")): return o
send({"id": 1, "cmd": "init", "cdb": cdb, "scripts": scripts}); print(wait(1))
deck = list(MAIN)
t = time.time()
send({"id": 2, "cmd": "search", "deck": deck, "extra": EXTRA, "hand": hand, "timeMs": int(float(secs) * 1000), "threads": threads,
      "top": 5, "maxActions": int(acts), "mode": mode, "targets": targets})
r = wait(2)
print("HAND", " + ".join(n(c) for c in hand), "| threads", threads, "| mode", mode or "hybrid", "| stats", r["stats"], "| complete", r["complete"])
for b in r["boards"]:
    print("\nSCORE %.2f  FIELD: %s  BACKROW: %s  HAND: %s" % (b["score"], ", ".join(n(c) for c in b["field"]), ", ".join(n(c) for c in b["backrow"]), ", ".join(n(c) for c in b["hand"])))
    for i, s in enumerate(b["steps"], 1):
        g = "; ".join("%s:%s" % (h, ", ".join(n(c) for c in cs)) for h, cs in s.get("groups", []))
        print("  %2d. %s %s %s %s" % (i, s["do"], n(s["card"]), ("| " + s["effect"][:50]) if s["effect"] else "", ("[" + g + "]") if g else ""))
send({"cmd": "quit"})
