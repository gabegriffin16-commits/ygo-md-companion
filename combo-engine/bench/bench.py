"""Deep-search benchmark: runs one hand through combo-engine with a big time and depth budget and prints the best boards.
Usage: python3 bench.py <engine> <cards.cdb> <scripts.zip> <hand codes comma-separated> <seconds> <maxActions> [threads] [mode] [targets]
Env: DECK_JSON, ZONES=0/1, STABLE=<fraction>, SIM=1 (rank by playing the opponent's turn; bench/sim.py shows why)."""
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
    if c == 0: return "(a drawn card)"
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
GHA = bool(os.environ.get("GITHUB_ACTIONS"))   # on GitHub, also post results as annotations (readable without logging in)
send({"id": 3, "cmd": "eval", "cards": sorted(set(MAIN + EXTRA))})
while True:
    o = json.loads(p.stdout.readline())
    if o.get("id") == 3: break
rv = ["%s (from %s, tag %r, max Lv %s)" % (v["name"], v["revive"]["from"], v["revive"]["tag"], v["revive"]["maxLevel"]) for v in o.get("eval", {}).values() if v.get("revive")]
print("REVIVERS", "; ".join(rv) or "none")
t = time.time()
send({"id": 2, "cmd": "search", "deck": deck, "extra": EXTRA, "hand": hand, "timeMs": int(float(secs) * 1000), "threads": threads,
      "top": 5, "maxActions": int(acts), "mode": mode, "targets": targets, **({"zones": os.environ["ZONES"] == "1"} if os.environ.get("ZONES") else {}), **({"stable": float(os.environ["STABLE"])} if os.environ.get("STABLE") else {}), "sim": bool(os.environ.get("SIM"))})
r = wait(2)
print("HAND", " + ".join(n(c) for c in hand), "| threads", threads, "| mode", mode or "hybrid", "| stats", r["stats"], "| complete", r["complete"])
for b in r["boards"]:
    z = b.get("zones") or []
    field = ", ".join(n(c) + (" [center]" if i < len(z) and z[i] == 2 else (" [EMZ]" if i < len(z) and z[i] >= 5 else "")) for i, c in enumerate(b["field"]))
    print("\nSCORE %.2f%s  FIELD: %s  BACKROW: %s  HAND: %s" % (b["score"], "  (text %.2f)" % b["sim"]["textScore"] if b.get("sim", {}).get("ok") else "", field,", ".join(n(c) for c in b["backrow"]), ", ".join(n(c) for c in b["hand"])))
    print("  GY: %s  BANISHED: %s" % (", ".join(n(c) for c in b.get("gy", [])), ", ".join(n(c) for c in b.get("banished", []))))
    for i, s in enumerate(b["steps"], 1):
        g = "; ".join("%s:%s" % (h, ", ".join(n(c) for c in cs)) for h, cs in s.get("groups", []))
        print("  %2d. %s %s %s %s" % (i, s["do"], n(s["card"]), ("| " + s["effect"][:50]) if s["effect"] else "", ("[" + g + "]") if g else ""))
if GHA:
    for k, b in enumerate(r["boards"][:2]):
        z = b.get("zones") or []
        field = ", ".join(n(c) + (" [center]" if i < len(z) and z[i] == 2 else "") for i, c in enumerate(b["field"]))
        print("::notice title=%s #%d::SCORE %.2f | FIELD: %s | BACKROW: %s | HAND: %s | %d steps | revivers: %s" % (" + ".join(n(c) for c in hand), k + 1, b["score"], field,
              ", ".join(n(c) for c in b["backrow"]), ", ".join(n(c) for c in b["hand"]), len(b["steps"]), len(rv)))
send({"cmd": "quit"})
