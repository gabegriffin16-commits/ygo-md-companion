"""Handtrap backups vs guides: for each guide combo that plays into a handtrap ("into Droll", "vs Nibiru", "through Ash"),
search the hand normally for the engine's main line, then let that handtrap hit each step it legally can (the engine's
interrupting search: oppHand + prefix + hitCard + hitStep) and search the continuation. Reports the guide's board after
the hit vs the engine's backup at the first step the handtrap can hit (closest to what guides show) and at the worst
step for us (the opponent picks the moment). All boards scored by the engine.
Mulcharmy (Fuwalos / Purulia) and Maxx "C" lines are left out: their "hit" makes the opponent draw, which the board score
doesn't count; they need their own goal (fewest summons / searches).
  python3 vsguide.py <engine> <cards.cdb> <scripts.zip> <refs.json>...
  env SECS (main search, 20), BSECS (per backup, 4), ACTS (backup max actions, 16), THREADS (8)
  FIRST=1: also search with the engine knowing the opponent plays the handtrap at its first legal chance ("oppFirst":
  the line built to play through it, like the guide's) and compare that board too."""
import json, os, re, sqlite3, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from guidehand import hand_of
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf = sys.argv[1:4]; refs = sys.argv[4:]
SECS = float(os.environ.get("SECS") or 20); BSECS = float(os.environ.get("BSECS") or 4); ACTS = int(os.environ.get("ACTS") or 16)
THREADS = int(os.environ.get("THREADS") or 8)
db = sqlite3.connect(cdb)
names = {}
for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id=d.id order by (d.alias != 0), d.id"): names.setdefault(nm.lower(), c)
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0]
HT = [("droll", "Droll & Lock Bird"), ("nibiru", "Nibiru, the Primal Being"), ("ash", "Ash Blossom & Joyous Spring"), ("imperm", "Infinite Impermanence"),
      ("veiler", "Effect Veiler"), ("ogre", "Ghost Ogre & Snow Rabbit"), ("belle", "Ghost Belle & Haunted Mansion")]
SKIP = re.compile(r"\b(fuwalos|purulia|mulcharmy|maxx)", re.I)
VS = re.compile(r"\b(into|vs|versus|through|under)\b", re.I)
FILLER = names["nibiru, the primal being"]
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
qid = [10]
def ask(o):
    qid[0] += 1; o["id"] = qid[0]
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready") or "score" in r): return r
ask({"cmd": "init", "cdb": cdb, "scripts": zipf})
rows = []
for rf in refs:
    R = json.load(open(rf, encoding="utf-8")); d = json.load(open(R["deck"]))
    for b in R["boards"]:
        low = b["name"].lower()
        hits = [full for key, full in HT if re.search(r"\b" + key, low)]
        if not hits or SKIP.search(low) or not VS.search(low): continue
        H = names[hits[0].lower()]
        hand = hand_of(db, names, b["start"], d["main"], FILLER)
        if not hand: print("%-14s %-44s skipped (start %s)" % (os.path.basename(rf)[:-5], b["name"][:44], b["start"])); continue
        deck = list(d["main"])
        for c in hand:
            if c in deck: deck.remove(c)
        C = lambda xs: [names[x.lower()] for x in xs if x.lower() in names]
        g = ask({"cmd": "score", "field": C(b.get("field", [])), "backrow": C(b.get("backrow", [])), "hand": C(b.get("hand", [])), "gy": C(b.get("gy", [])), "banished": C(b.get("banished", [])), "extra": d["extra"], "deck": d["main"]})["score"]["total"]
        base = {"cmd": "search", "deck": deck, "extra": d["extra"], "hand": hand, "threads": THREADS, "top": 1, "maxActions": 40}
        r = ask(dict(base, timeMs=int(SECS * 1000), labels=True))
        if not r.get("boards"): continue
        main = r["boards"][0]; first = worst = None
        for i, st in enumerate(main["steps"]):
            rb = ask(dict(base, timeMs=int(BSECS * 1000), maxActions=ACTS, oppHand=[H], prefix=main["labels"], hitCard=H, hitStep=i))
            if not rb.get("boards"): continue   # the handtrap couldn't hit here
            s = rb["boards"][0]["score"]
            if first is None: first = (i, s)
            if worst is None or s < worst[1]: worst = (i, s)
        knew = None
        if os.environ.get("FIRST"):
            rk = ask(dict(base, timeMs=int(SECS * 1000), oppFirst=H))
            if rk.get("boards"): knew = rk["boards"][0]["score"]
        rows.append((os.path.basename(rf)[:-5], b["name"], nm(H), g, main["score"], first, worst, knew))
        f = "step %d: %.2f" % first if first else "never hit"
        w = "step %d: %.2f" % worst if worst else "-"
        print("%-14s %-44s %-16s guide after hit %6.2f | engine main %6.2f, first hit %s, worst %s%s" % (rows[-1][0], b["name"][:44], nm(H)[:16], g, main["score"], f, w,
              (" | knowing it's coming %.2f" % knew) if knew is not None else ""), flush=True)
hit = [x for x in rows if x[5]]
print("\n%d guide 'vs handtrap' lines; the handtrap could hit the engine's line in %d" % (len(rows), len(hit)))
if hit:
    print("engine backup at first hit >= guide's board: %d of %d; worst case >= guide: %d of %d" % (sum(1 for x in hit if x[5][1] >= x[3] - 0.3), len(hit), sum(1 for x in hit if x[6][1] >= x[3] - 0.3), len(hit)))
k = [x for x in rows if x[7] is not None]
if k: print("engine knowing it's coming >= guide's board: %d of %d" % (sum(1 for x in k if x[7] >= x[3] - 0.3), len(k)))
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
