"""Guide data from real hands: deal random 5-card hands from the actual deck and search them, then summarize per starter.
Pass 1 (deck sample): M random hands -> the deck's opening quality and what a strong board is.
Pass 2 (every non-handtrap card, so the guide is complete): N random hands that contain it -> its role (starter /
extender / situational, from how the lines used it); for starters also -> reliability (share reaching a good board), the main line (best
line using the starter + at most one other card from the hand: a guide's "+1"), upgrades (cards whose presence raises the
board most, each with its best line), and how the main line holds up against each handtrap (oppFirst search).
Writes the guide data model (JSON) for the Guide Crafter page.
  python3 starters.py <engine> <cards.cdb> <scripts.zip> <deck.json> <out.json>
  env M (deck sample, 40), N (hands per starter, 12), OPEN (starter if it opens >= this share of its lines, 0.3), USED (extender, 0.5), SECS (per search, 6), THREADS (12), SEED (1)"""
import json, os, random, sqlite3, subprocess, sys, statistics
from collections import Counter, defaultdict
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, deckf, outf = sys.argv[1:6]
M = int(os.environ.get("M") or 40); N = int(os.environ.get("N") or 12); K = int(os.environ.get("K") or 6)
SECS = float(os.environ.get("SECS") or 6); THREADS = int(os.environ.get("THREADS") or 12); random.seed(int(os.environ.get("SEED") or 1))
db = sqlite3.connect(cdb)
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0]
HT = {14558127: "Ash Blossom", 10045474: "Infinite Impermanence", 97268402: "Effect Veiler", 94145021: "Droll & Lock Bird", 27204311: "Nibiru"}
d = json.load(open(deckf)); main = list(d["main"]); extra = d["extra"]
def is_ht(c):
    """A handtrap (or a Trap): not a starter, whatever the line did with it (a discard cost, a Set)."""
    r = db.execute("select d.type, t.desc from datas d join texts t on t.id=d.id where d.id=?", (c,)).fetchone()
    if not r: return True
    typ, desc = r[0], r[1].lower()
    if c in HT or typ & 0x4: return True
    return "(quick effect)" in desc and any(x in desc for x in ("discard this card", "send this card from your hand", "this card in your hand", "reveal this card"))
def info(c):
    r = db.execute("select d.type, d.setcode, t.name, t.desc from datas d join texts t on t.id=d.id where d.id=?", (c,)).fetchone()
    return r or (0, 0, str(c), "")
def sets(c):
    sc = info(c)[1] or 0
    return {(sc >> (16 * i)) & 0xfff for i in range(4) if (sc >> (16 * i)) & 0xffff}
ALL = sorted(set(main) | set(extra))
def engine(c):
    """Part of the deck's engine: shares an archetype with 2+ other cards of the deck, or names / is named by one of them."""
    if c in HT: return False
    mine = sets(c)
    if mine and sum(1 for o in ALL if o != c and sets(o) & mine) >= 2: return True
    n, t = info(c)[2], info(c)[3]
    return any(o != c and ('"%s"' % n in info(o)[3] or '"%s"' % info(o)[2] in t) for o in ALL)
is_ht_text = is_ht
is_ht = lambda c: (c in HT or not engine(c)) if c in main else is_ht_text(c)   # openers: engine cards only (Faimena counts, Ash doesn't)
qid = [10]; crashes = []
def start():
    global p
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
    ask({"cmd": "init", "cdb": cdb, "scripts": zipf})
def ask(o):
    qid[0] += 1; o["id"] = qid[0]; p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        line = p.stdout.readline()
        if not line:   # the engine died: keep the request for a repro, start a new one, skip this search
            crashes.append(o); json.dump(crashes, open(outf + ".crash.json", "w"))
            print("ENGINE CRASH (exit %s); request saved to %s.crash.json" % (p.wait(), outf), flush=True)
            start(); return {}
        r = json.loads(line)
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready") or "score" in r): return r
start()
def search(hand, **kw):
    deck = list(main)
    for c in hand: deck.remove(c)
    r = ask(dict({"cmd": "search", "deck": deck, "extra": extra, "hand": hand, "timeMs": int(SECS * 1000), "threads": THREADS, "top": 1, "maxActions": 40, "labels": True}, **kw))
    return r["boards"][0] if r.get("boards") else None
def deal(must=None):
    pool = list(main)
    hand = []
    if must: pool.remove(must); hand.append(must)
    hand += random.sample(pool, 5 - len(hand))
    return hand
def used(b, hand):
    """Hand cards the line actually used (as the acting card or a pick), each copy once."""
    left = Counter(hand); out = []
    for s in b["steps"]:
        for c in [s["card"]] + s.get("picks", []):
            if left.get(c, 0) > 0: left[c] -= 1; out.append(c)
    return out
def brief(b, hand):
    return {"hand": hand, "score": round(b["score"], 2), "field": b["field"], "zones": b.get("zones", []), "backrow": b["backrow"], "left": b["hand"], "gy": b.get("gy", []), "banished": b.get("banished", []),
            "steps": [[s["do"], s["card"], s.get("effect", ""), s.get("picks", []), s.get("groups", [])] for s in b["steps"]], "labels": b.get("labels", []), "uses": used(b, hand)}
# ---- pass 1: the deck's openings ----
sample = []
for i in range(M):
    h = deal(); b = search(h)
    if b: sample.append(brief(b, h))
    print("deck sample %d/%d: %.2f  %s" % (i + 1, M, b["score"] if b else 0, ", ".join(nm(c) for c in h)), flush=True)
scores = [x["score"] for x in sample]
good = round(statistics.quantiles(scores, n=4)[2], 2) if len(scores) >= 4 else max(scores)   # "a good board": the top quarter's floor
def opener_of(x):
    acts = [s[1] for s in x["steps"] if s[1] in x["hand"] and s[0] in ("Normal Summon", "Special Summon", "Activate") and not is_ht(s[1])]
    return acts[0] if acts else None
for x in sample: x["opener"] = opener_of(x)
# ---- pass 2: every card of the deck (a complete guide: no card left out) ----
# Each non-handtrap card gets its own hands (the deck sample's + N dealt with it). Its role comes from what the lines did:
# 1-card starter = alone with the deck's 4 most common non-engine cards, the board it builds (field, set cards, GY,
# banished; cards still held don't count) scores >= SOLO: its line is a true 1-card combo (handtraps in hand may serve as
# material or a cost, as in real hands); 2-card starter = the line opens with it in >= OPEN of its hands; extender = used in >= USED of
# them; else situational. Non-engine cards (handtraps, staples) are listed, not checked.
OPEN = float(os.environ.get("OPEN") or 0.3); USED = float(os.environ.get("USED") or 0.5); SOLO = float(os.environ.get("SOLO") or 4.0)
cards = sorted(set(c for c in main if engine(c)), key=lambda c: -main.count(c))
non_engine = sorted(set(c for c in main if not engine(c)))
pool = sorted([c for c in main if not engine(c)], key=lambda c: (-main.count(c), c))
brick_hand = pool[:5] if len(pool) >= 5 else None
def built(L):
    """Score of the board a line built, without the cards still in hand."""
    r = ask({"cmd": "score", "field": L["field"], "zones": L.get("zones", []), "backrow": L["backrow"], "hand": [], "gy": L.get("gy", []), "banished": L.get("banished", []), "extra": extra, "deck": main})
    return r["score"]["total"] if r.get("score") else 0
print("non-engine filler: %s" % ", ".join(nm(c) for c in (brick_hand or [])[:4]), flush=True)
out = {"deck": os.path.basename(deckf), "searchSecs": SECS, "opening": {"hands": len(sample), "median": statistics.median(scores), "good": good,
       "shareGood": round(sum(1 for s in scores if s >= good) / len(scores), 2)}, "cards": [], "nonEngine": non_engine, "filler": (brick_hand or [])[:4], "soloMin": SOLO, "starters": []}
print("good board >= %.2f; %d cards to check, %d non-engine" % (good, len(cards), len(non_engine)), flush=True)
for st in cards:
    hands = [x for x in sample if st in x["hand"]]
    for i in range(N):
        h = deal(st); b = search(h)
        if b: x = brief(b, h); x["opener"] = opener_of(x); hands.append(x)
    if not hands: continue
    out.setdefault("hands", {})[str(st)] = [{"hand": x["hand"], "score": x["score"], "uses": x["uses"], "opener": x["opener"]} for x in hands]   # for later re-analysis
    sc = [x["score"] for x in hands]; med = statistics.median(sc)
    solo = None
    if brick_hand:
        sh = [st] + [c for c in brick_hand if c != st][:4]
        b = search(sh)
        if b: solo = brief(b, sh); solo["opener"] = opener_of(solo)
    opens = sum(1 for x in hands if x["opener"] == st) / len(hands)
    usedr = sum(1 for x in hands if st in x["uses"]) / len(hands)
    gain = built(solo) if solo else 0
    role = "starter" if gain >= SOLO or opens >= OPEN else "extender" if usedr >= USED else "situational"
    kind = ("1-card" if gain >= SOLO else "2-card") if role == "starter" else None
    base = {"card": st, "copies": main.count(st), "role": role, "hands": len(hands), "median": round(med, 2), "opens": round(opens, 2), "used": round(usedr, 2),
            "reliability": round(sum(1 for s in sc if s >= good) / len(sc), 2), "kind": kind, "soloBuilt": round(gain, 2), "solo": solo,
            "soloFiller": [c for c in (solo or {}).get("uses", []) if not engine(c)]}
    users = [x for x in hands if st in x["uses"]]
    if users: base["best"] = max(users, key=lambda x: x["score"])   # its best line (extenders / situational: where it shines)
    out["cards"].append({k: v for k, v in base.items() if k != "best"} | ({"best": base["best"]} if users else {}))
    print("%s: %s%s (alone builds %.1f, opens %d%%, used %d%%), %d hands, median %.2f, good in %d%%" % (nm(st), role, " " + kind if kind else "", gain, round(opens * 100), round(usedr * 100), len(hands), med, round(base["reliability"] * 100)), flush=True)
    if role != "starter": json.dump(out, open(outf, "w"), indent=1); continue
    # Main line: the best line using the starter + at most one other card of the hand.
    # (Non-engine cards spent as material or a cost don't count: any spare card does that.)
    lean = [x for x in hands if len([c for c in x["uses"] if c != st and engine(c)]) <= 1 and st in x["uses"]]
    # None: the line needing the fewest other cards among hands at or above the median (not the best 5-card line).
    extra_n = lambda x: len([c for c in x["uses"] if c != st and engine(c)])
    # A 1-card starter's main line is its line alone (with non-engine cards): the guide's "1-card combo".
    mainline = solo if kind == "1-card" else max(lean, key=lambda x: x["score"]) if lean else min([x for x in hands if x["score"] >= med], key=lambda x: (extra_n(x), -x["score"]))
    # Upgrades: cards (other than the starter) present and used in hands that score clearly above the starter's median.
    with_c = defaultdict(list)
    for x in hands:
        for c in set(x["hand"]) - {st}:
            if c in x["uses"]: with_c[c].append(x)
    ups = []
    for c, xs in with_c.items():
        best = max(xs, key=lambda x: x["score"])
        if best["score"] > max(med, mainline["score"]) + 1.0: ups.append({"card": c, "gain": round(best["score"] - mainline["score"], 2), "line": best})
    ups.sort(key=lambda u: -u["gain"])
    vs = []
    for ht in HT:
        b = search(mainline["hand"], oppFirst=ht)
        if not b: continue
        hit = any(s["do"] == "Opp" for s in b["steps"])
        # Relative to the main line, capped at it: a separate search can land a little higher by chance.
        keep = min(1.0, b["score"] / mainline["score"]) if mainline["score"] > 0 else 0
        vs.append({"by": ht, "used": hit, "score": round(min(b["score"], mainline["score"]), 2), "keeps": round(keep, 2) if hit else 1.0, "line": brief(b, mainline["hand"]) if hit else None})
    entry = dict(base, main=mainline, upgrades=ups[:3], vs=vs); entry.pop("best", None)
    out["starters"].append(entry); json.dump(out, open(outf, "w"), indent=1)   # saved after each card
    print("  main %.2f (uses %s), upgrades %s, vs %s" % (mainline["score"], ", ".join(nm(c) for c in mainline["uses"]), ", ".join("%s +%.1f" % (nm(u["card"]), u["gain"]) for u in ups[:3]) or "-",
          " ".join("%s %d%%%s" % (HT[v["by"]].split()[0], round(v["keeps"] * 100), "" if v["used"] else "(n/a)") for v in vs)), flush=True)
json.dump(out, open(outf, "w"), indent=1)
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
print("wrote", outf)
