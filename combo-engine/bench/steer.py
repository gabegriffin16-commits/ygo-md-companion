"""Prototype: does aiming the search at the deck's boss monsters find better boards than one plain search?
For each guide hand in a refs file: one plain search (S seconds), then K short searches each with one candidate boss as a
goal (S/K... each), every board re-scored WITHOUT the goal bonus (score cmd), best kept. Candidates come from the deck,
not the guide (works for guideless decks): monsters with the highest text interruption value, skipping handtraps.
  python3 steer.py <engine> <cards.cdb> <scripts.zip> <refs.json> <plain secs> <steer secs each> [K=4] [name filter]"""
import json, os, sqlite3, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, refsf, plain, each = sys.argv[1:7]; K = int(sys.argv[7]) if len(sys.argv) > 7 else 4; only = sys.argv[8].lower() if len(sys.argv) > 8 else ""
db = sqlite3.connect(cdb)
names = {}
for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id=d.id order by (d.alias != 0), d.id"): names.setdefault(nm.lower(), c)
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0]
R = json.load(open(refsf, encoding="utf-8")); boards = R["boards"] if isinstance(R, dict) else R
deckf = R.get("deck") if isinstance(R, dict) else None
d = json.load(open(os.path.join(os.path.dirname(refsf), "..", deckf) if deckf and not os.path.exists(deckf) else deckf))
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
qid = [10]
def ask(o):
    qid[0] += 1; o["id"] = qid[0]
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready") or "eval" in r or "score" in r): return r
ask({"cmd": "init", "cdb": cdb, "scripts": zipf})
cards = sorted(set(d["main"]) | set(d["extra"]))
ev = ask({"cmd": "eval", "cards": cards})["eval"]
HT = {"Ash Blossom & Joyous Spring", "Effect Veiler", "Maxx \"C\"", "Droll & Lock Bird", "Mulcharmy Fuwalos", "Nibiru, the Primal Being", "Infinite Impermanence", "Ghost Ogre & Snow Rabbit", "Ghost Belle & Haunted Mansion", "Mulcharmy Purulia"}
def mon(c): return db.execute("select type from datas where id=?", (c,)).fetchone()[0] & 1
cand = sorted([c for c in cards if mon(c) and nm(c) not in HT], key=lambda c: -max(ev[str(c)].get("field", 0), ev[str(c)].get("onSummon", 0)))[:K]
print("candidate bosses:", ", ".join(nm(c) for c in cand))
def rescore(b):
    return ask({"cmd": "score", "field": b["field"], "zones": b.get("zones", []), "backrow": b["backrow"], "hand": b["hand"], "gy": b.get("gy", []), "banished": b.get("banished", []), "extra": d["extra"], "deck": d["main"]})["score"]["total"]
tp, ts, n = 0, 0, 0
for b in boards:
    if only and only not in b["name"].lower(): continue
    hand = []
    for x in b["start"]:
        c = names.get(x.lower())
        hand.append(c if c else names["nibiru, the primal being"])
    deck = list(d["main"])
    for c in hand:
        if c in deck: deck.remove(c)
    base = {"cmd": "search", "deck": deck, "extra": d["extra"], "hand": hand, "threads": 12, "top": 1, "maxActions": 40}
    r = ask(dict(base, timeMs=int(float(plain) * 1000)))
    best = rescore(r["boards"][0]) if r.get("boards") else 0
    steered = best
    for c in cand:
        r = ask(dict(base, timeMs=int(float(each) * 1000), targets=[c]))
        if r.get("boards"): steered = max(steered, rescore(r["boards"][0]))
    n += 1; tp += best; ts += steered
    C = lambda xs: [names[x.lower()] for x in xs if x.lower() in names]
    g = rescore({"field": C(b.get("field", [])), "backrow": C(b.get("backrow", [])), "hand": C(b.get("hand", [])), "gy": C(b.get("gy", [])), "banished": C(b.get("banished", []))})
    print("%-50s plain %6.2f  steered %6.2f  guide %6.2f" % (b["name"][:50], best, steered, g))
print("total over %d hands: plain %.2f  plain+steered %.2f" % (n, tp, ts))
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
