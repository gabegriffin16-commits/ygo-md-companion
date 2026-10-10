"""Search a hand with the opponent's-turn simulation on and show, per board, the text score vs the simulated score,
which probe plays were stopped and by what, and which interruptions never got a chance (counted at half).
Usage: python3 sim.py <engine> <cards.cdb> <scripts.zip> <deck.json> <hand names or codes, ;-separated> <secs> [top]
Env: THREADS (default: all cores), ACTS (max actions, default 40), TARGETS (goal names/codes, ;-separated)."""
import json, os, sqlite3, subprocess, sys
exe, cdb, scripts, deckf, hand, secs = sys.argv[1:7]
top = int(sys.argv[7]) if len(sys.argv) > 7 else 6
db = sqlite3.connect(cdb)
names = {}
for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id = d.id order by (d.alias != 0), d.id"): names.setdefault(nm.lower(), c)
def code(x): x = x.strip(); return int(x) if x.isdigit() else names[x.lower()]
def n(c):
    if not c: return "(lasting effect)"
    r = db.execute("select name from texts where id=?", (c,)).fetchone(); return r[0] if r else str(c)
deck = json.load(open(deckf))
h = [code(x) for x in hand.split(";") if x.strip()]
targets = [code(x) for x in os.environ.get("TARGETS", "").split(";") if x.strip()]
main = list(deck["main"])
for c in h:
    if c in main: main.remove(c)
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready")): return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": scripts})
r = ask({"id": 2, "cmd": "search", "deck": main, "extra": deck["extra"], "hand": h, "timeMs": int(float(secs) * 1000), "top": top, "sim": True,
         "threads": int(os.environ.get("THREADS") or os.cpu_count()), "maxActions": int(os.environ.get("ACTS") or 40), "targets": targets})
print("hand:", " + ".join(n(c) for c in h), "| stats:", {k: r["stats"][k] for k in ("seconds", "bestAt", "simSeconds")})
for b in r.get("boards", []):
    s = b.get("sim", {})
    print("\nSIM %6.2f  (text %6.2f)  FIELD: %s  BACKROW: %s  HAND: %s" % (b["score"], s.get("textScore", b["score"]), ", ".join(n(c) for c in b["field"]), ", ".join(n(c) for c in b["backrow"]), ", ".join(n(c) for c in b["hand"])))
    if not s.get("ok"): print("   simulation failed (kept the text score)"); continue
    print("   plays: " + " | ".join("%s: %s" % (t["play"], "STOPPED" if t["stopped"] else "went through" if t["tried"] else "never came") for t in s["plays"]))
    for c in s["credits"]: print("   stopped one with %s (%.2f) on: %s" % (n(c["card"]), c["value"], c.get("play", "")))
    for c in s["untested"]: print("   never got a chance: %s (half: %.2f)" % (n(c["card"]), c["value"]))
    print("   (%d play-throughs)" % s["runs"])
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
