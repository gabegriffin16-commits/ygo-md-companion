"""Why does the plain search miss a long combo? 1) search the hand with the guide's end-board cards as goals to get a line
that reaches them; 2) run a plain search with that line traced (MDC_TRACE): at each beam level, where the line's state
ranks and its promise broken into parts (board score, hand, GY, moves) next to the top state's.
  python3 tracegoal.py <engine> <cdb> <zip> <deck.json> "<hand codes or names, comma-separated>" "<goal names>" [secs]
Names are matched against the deck (exact first, then substring). env THREADS (12), GOALSECS (30), MODE (beam)."""
import json, os, sqlite3, subprocess, sys, tempfile
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, deckf, handarg, goalarg = sys.argv[1:7]
secs = float(sys.argv[7]) if len(sys.argv) > 7 else 20
THREADS = int(os.environ.get("THREADS") or 12); GOALSECS = float(os.environ.get("GOALSECS") or 30); MODE = os.environ.get("MODE") or "beam"
db = sqlite3.connect(cdb); d = json.load(open(deckf))
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0]
pool = sorted(set(d["main"]) | set(d["extra"]))
def code(t):
    t = t.strip()
    if t.isdigit(): return int(t)
    ex = [c for c in pool if nm(c).lower() == t.lower()]
    sub = [c for c in pool if t.lower() in nm(c).lower()]
    if not (ex or sub): sys.exit("no card matches %r" % t)
    return (ex or sub)[0]
hand = [code(t) for t in handarg.split(",") if t.strip()]
goals = [code(t) for t in goalarg.split(",") if t.strip()]
deck = list(d["main"])
for c in hand:
    if c in deck: deck.remove(c)   # a filler from outside the deck (compare.py's Nibiru: give its code) stays out
req = {"cmd": "search", "deck": deck, "extra": d["extra"], "hand": hand, "threads": THREADS, "top": 1, "maxActions": 40, "labels": True}
def run(o, env=None):
    # One request at a time: a "quit" sent while a search runs stops it.
    errf = tempfile.TemporaryFile("w+", encoding="utf-8")
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errf, text=True, encoding="utf-8", bufsize=1, env=env)
    res = {}
    for m in ({"cmd": "init", "cdb": cdb, "scripts": zipf, "id": 1}, dict(o, id=2)):
        p.stdin.write(json.dumps(m) + "\n"); p.stdin.flush()
        while True:
            line = p.stdout.readline()
            if not line: break
            r = json.loads(line)
            if r.get("id") == m["id"] and (r.get("ready") or r.get("done") or r.get("error")): res = r; break
    p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush(); p.wait()
    errf.seek(0)
    return res, errf.read()
print("hand:", ", ".join(nm(c) for c in hand)); print("goals:", ", ".join(nm(c) for c in goals))
g, _ = run(dict(req, timeMs=int(GOALSECS * 1000), targets=goals))
b = (g.get("boards") or [None])[0]
if not b: sys.exit("goal search found nothing")
got = [c for c in goals if c in b["field"] + b["backrow"]]
print("goal line: %.2f, reaches %d/%d goals, %d steps; field: %s | backrow: %s" % (b["score"], len(got), len(goals), len(b["steps"]), ", ".join(nm(c) for c in b["field"]), ", ".join(nm(c) for c in b["backrow"])))
tf = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False); json.dump(b["labels"], tf); tf.close()
env = dict(os.environ, MDC_TRACE=tf.name)
p, err = run(dict(req, timeMs=int(secs * 1000), mode=MODE), env)
pb = (p.get("boards") or [None])[0]
print("plain search: %.2f (goal line without goal bonus: see its 'board' at the last level)" % (pb["score"] if pb else 0))
for l in err.splitlines():
    if l.startswith("trace") or l.startswith("   top"): print(l)
os.unlink(tf.name)
