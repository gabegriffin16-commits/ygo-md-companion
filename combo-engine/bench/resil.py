"""Prototype for resilience: does choosing among the engine's top lines by how well they survive handtraps get closer
to guide play? For each guide hand (normal lines, not the "vs handtrap" ones), take the engine's top K distinct lines;
for each, the worst-case backup against each handtrap in the set over every step it can hit (interrupting search);
print the unhit score, the worst backup, and which line a blend (1-w)*unhit + w*worst picks vs the plain best.
  python3 resil.py <engine> <cards.cdb> <scripts.zip> <refs.json> [name filter]
  env SECS (main search, 20), K (lines, 5), BSECS (per backup, 2), W (blend weight, 0.4), HT (handtraps, ;-separated names;
  default Ash, Imperm, Droll, Nibiru), THREADS (12)"""
import json, os, re, sqlite3, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from guidehand import hand_of
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, rf = sys.argv[1:5]; only = sys.argv[5].lower() if len(sys.argv) > 5 else ""
SECS = float(os.environ.get("SECS") or 20); K = int(os.environ.get("K") or 5); BSECS = float(os.environ.get("BSECS") or 2)
W = float(os.environ.get("W") or 0.4); THREADS = int(os.environ.get("THREADS") or 12)
db = sqlite3.connect(cdb)
names = {}
for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id=d.id order by (d.alias != 0), d.id"): names.setdefault(nm.lower(), c)
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0]
HT = [names[x.strip().lower()] for x in (os.environ.get("HT") or "Ash Blossom & Joyous Spring;Infinite Impermanence;Droll & Lock Bird;Nibiru, the Primal Being").split(";") if x.strip()]
SPECIAL = re.compile(r"\b(into|vs|versus|through|under|otk|fuwalos|purulia|mulcharmy|maxx)\b", re.I)
FILLER = names["nibiru, the primal being"]
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
qid = [10]
def ask(o):
    qid[0] += 1; o["id"] = qid[0]
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready")): return r
ask({"cmd": "init", "cdb": cdb, "scripts": zipf})
R = json.load(open(rf, encoding="utf-8")); d = json.load(open(R["deck"]))
tot = {"best_unhit": 0, "best_worst": 0, "pick_unhit": 0, "pick_worst": 0, "n": 0, "changed": 0}
for b in R["boards"]:
    if SPECIAL.search(b["name"]) or (only and only not in b["name"].lower()): continue
    hand = hand_of(db, names, b["start"], d["main"], FILLER)
    if not hand: continue
    deck = list(d["main"])
    for c in hand:
        if c in deck: deck.remove(c)
    base = {"cmd": "search", "deck": deck, "extra": d["extra"], "hand": hand, "threads": THREADS, "maxActions": 40}
    r = ask(dict(base, timeMs=int(SECS * 1000), top=K, labels=True))
    lines = []
    for L in r.get("boards", [])[:K]:
        worst = L["score"]; at = "-"
        for H in HT:
            for i in range(len(L["steps"])):
                rb = ask(dict(base, timeMs=int(BSECS * 1000), top=1, maxActions=16, oppHand=[H], prefix=L["labels"], hitCard=H, hitStep=i))
                if rb.get("boards") and rb["boards"][0]["score"] < worst: worst = rb["boards"][0]["score"]; at = "%s at step %d" % (nm(H).split(",")[0][:14], i)
        lines.append((L["score"], worst, at, ", ".join(nm(c) for c in L["field"])[:70]))
    if not lines: continue
    best = lines[0]; pick = max(lines, key=lambda x: (1 - W) * x[0] + W * x[1])
    tot["n"] += 1; tot["best_unhit"] += best[0]; tot["best_worst"] += best[1]; tot["pick_unhit"] += pick[0]; tot["pick_worst"] += pick[1]; tot["changed"] += pick is not best
    print("\n## %s" % b["name"])
    for x in lines: print("   %s unhit %6.2f  worst %6.2f (%s)  %s" % ("*" if x is pick else " ", x[0], x[1], x[2], x[3]), flush=True)
if tot["n"]:
    n = tot["n"]
    print("\n%d hands, resilient pick differs on %d: plain best unhit %.2f / worst %.2f per hand; resilient pick unhit %.2f / worst %.2f" % (
        n, tot["changed"], tot["best_unhit"] / n, tot["best_worst"] / n, tot["pick_unhit"] / n, tot["pick_worst"] / n))
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
