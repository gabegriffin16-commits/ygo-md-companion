"""Guide data from real hands: deal random 5-card hands from the actual deck and search them, then summarize per starter.
Pass 1 (deck sample): M random hands -> the deck's opening quality, and the starters (cards the lines open with).
Pass 2 (per starter): N random hands that contain it -> reliability (share reaching a good board), the main line (best
line using the starter + at most one other card from the hand: a guide's "+1"), upgrades (cards whose presence raises the
board most, each with its best line), and how the main line holds up against each handtrap (oppFirst search).
Writes the guide data model (JSON) for the Guide Crafter page.
  python3 starters.py <engine> <cards.cdb> <scripts.zip> <deck.json> <out.json>
  env M (deck sample, 40), N (hands per starter, 12), K (starters, 6), SECS (per search, 6), THREADS (12), SEED (1)"""
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
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready")): return r
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
opener = Counter()
for x in sample:
    acts = [s[1] for s in x["steps"] if s[1] in x["hand"] and s[0] in ("Normal Summon", "Special Summon", "Activate") and not is_ht(s[1])]
    if acts: opener[acts[0]] += x["score"]   # weighted by how good the board it opened was
starters = [c for c, _ in opener.most_common(K)]
print("starters:", ", ".join(nm(c) for c in starters), "| good board >= %.2f" % good, flush=True)
# ---- pass 2: per starter ----
out = {"deck": os.path.basename(deckf), "searchSecs": SECS, "opening": {"hands": len(sample), "median": statistics.median(scores), "good": good,
       "shareGood": round(sum(1 for s in scores if s >= good) / len(scores), 2)}, "starters": []}
for st in starters:
    hands = [x for x in sample if st in x["hand"]]
    for i in range(N):
        h = deal(st); b = search(h)
        if b: hands.append(brief(b, h))
    sc = [x["score"] for x in hands]
    # Main line: the best line using the starter + at most one other card of the hand.
    lean = [x for x in hands if len([c for c in x["uses"] if c != st]) <= 1 and st in x["uses"]]
    # None: the line needing the fewest other cards among hands at or above the median (not the best 5-card line).
    med0 = statistics.median(sc)
    extra_n = lambda x: len([c for c in x["uses"] if c != st])
    mainline = max(lean, key=lambda x: x["score"]) if lean else min([x for x in hands if x["score"] >= med0], key=lambda x: (extra_n(x), -x["score"]))
    # Upgrades: cards (other than the starter) present in hands that score clearly above the starter's median.
    with_c = defaultdict(list)
    for x in hands:
        for c in set(x["hand"]) - {st}:
            if c in x["uses"]: with_c[c].append(x)
    med = statistics.median(sc)
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
    entry = {"card": st, "hands": len(hands), "median": round(med, 2), "reliability": round(sum(1 for s in sc if s >= good) / len(sc), 2), "main": mainline, "upgrades": ups[:3], "vs": vs}
    out["starters"].append(entry); json.dump(out, open(outf, "w"), indent=1)   # saved after each starter
    print("%s: %d hands, median %.2f, good in %d%%, main %.2f (uses %s), upgrades %s, vs %s" % (nm(st), len(hands), med, round(entry["reliability"] * 100), mainline["score"],
          ", ".join(nm(c) for c in mainline["uses"]), ", ".join("%s +%.1f" % (nm(u["card"]), u["gain"]) for u in ups[:3]) or "-",
          " ".join("%s %d%%%s" % (HT[v["by"]].split()[0], round(v["keeps"] * 100), "" if v["used"] else "(n/a)") for v in vs)), flush=True)
json.dump(out, open(outf, "w"), indent=1)
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
print("wrote", outf)
