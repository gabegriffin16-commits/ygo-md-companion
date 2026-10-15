"""Guide data from real hands: deal random 5-card hands from the actual deck and search them, then summarize per starter.
Pass 1 (deck sample): M random hands -> the deck's opening quality and what a strong board is.
Pass 2 (every non-handtrap card, so the guide is complete): N random hands that contain it -> its role (starter /
extender / situational, from how the lines used it); for starters also -> reliability (share reaching a good board), the main line (best
line using the starter + at most one other card from the hand: a guide's "+1"), upgrades (cards whose presence raises the
board most, each with its best line), and how the main line holds up against each handtrap (oppFirst search).
Writes the guide data model (JSON) for the Guide Crafter page.
  python3 starters.py <engine> <cards.cdb> <scripts.zip> <deck.json> <out.json>
  env M (shared hand pool, 100: every hand counts for every engine card in it), N (at least this many hands per card: topped up, 12), STABLE (early stop for the single-card / pair / handtrap searches, 0.4), USED (extender, 0.5), SECS (per search, 6), THREADS (12), SEED (1)"""
import json, os, random, sqlite3, subprocess, sys, statistics, time
T0 = time.time()
from collections import Counter, defaultdict
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, deckf, outf = sys.argv[1:6]
M = int(os.environ.get("M") or 100); STABLE = float(os.environ.get("STABLE") or 0.4); N = int(os.environ.get("N") or 12); K = int(os.environ.get("K") or 6)
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
import re
QUOTES = {o: set(q for q in re.findall(r'"([^"]{4,})"', info(o)[3])) for o in ALL}
USED_ENGINE = set()   # filled after the deck sample: cards the lines use in most hands holding them (Bonfire, One for One)
def engine(c):
    """Part of the deck's engine: shares an archetype with 2+ other cards of the deck, names / is named by one of them (a
    quoted name or archetype inside a card's name counts: "Diabellstar"), or the lines use it in most hands holding it."""
    if c in HT: return False
    if c in USED_ENGINE: return True
    mine = sets(c)
    if mine and sum(1 for o in ALL if o != c and sets(o) & mine) >= 2: return True
    n = info(c)[2]
    # (A Main Deck card quoting an Extra Deck card's name doesn't count: Magicians' Souls quotes "Dark Magician".)
    return any(o != c and (any(q in n for q in QUOTES[o]) or (o in main and any(q in info(o)[2] for q in QUOTES[c]))) for o in ALL)
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
from explain import explain
def opener_of(x):
    """The hand card the line opens with: the first one whose step leads to end-board points (explain.py's trace). A free
    activation that builds nothing (Crossout Designator played first because it costs nothing) isn't the opener."""
    w = ask({"cmd": "score", "field": x["field"], "zones": x.get("zones", []), "backrow": x["backrow"], "hand": x.get("left", []), "gy": x.get("gy", []), "banished": x.get("banished", []), "extra": extra, "deck": main})
    ex = explain(x, w["score"], nm, lambda c: info(c)[0])[0] if w.get("score") else [{"points": 1}] * len(x["steps"])
    # Cards of the hand used in steps that lead to end-board points (a free activation that builds nothing isn't "used").
    left = Counter(x["hand"]); x["useful"] = []
    x["acted"] = [st[1] for st, e in zip(x["steps"], ex) if e["points"] > 0 and st[1] in x["hand"]]   # cards that acted themselves (not a cost or material pick)
    for st, e in zip(x["steps"], ex):
        if e["points"] <= 0: continue
        for c in [st[1]] + list(st[3]):
            if left.get(c, 0) > 0: left[c] -= 1; x["useful"].append(c)
    # A Trap in hand can't open turn one (a copy the line activated was Set from the Deck by an effect), unless it says
    # it can be activated from the hand.
    trap_ok = lambda c: not (info(c)[0] & 0x4) or "activate this card from your hand" in info(c)[3].lower()
    acts = [s[1] for s, e in zip(x["steps"], ex) if s[1] in x["hand"] and s[0] in ("Normal Summon", "Special Summon", "Activate") and not is_ht(s[1]) and trap_ok(s[1]) and e["points"] > 0]
    return acts[0] if acts else None
# Cards the lines use in at least half of the sample hands holding them (2+ hands) are engine, whatever their wording.
# (Counted on plays where the card itself acts and leads to end-board points: a card spent as a cost, like Triple Tactics
# Talent sent for Diabellstar's summon, isn't one.)
for x in sample: opener_of(x)   # fills x["useful"]
for c in set(main):
    xs = [x for x in sample if c in x["hand"]]
    if c not in HT and not is_ht_text(c) and len(xs) >= 2 and sum(1 for x in xs if c in x["acted"]) >= 0.25 * len(xs): USED_ENGINE.add(c)   # a quarter: a 1-of often sits behind a stronger starter (Magicians' Souls)
for x in sample: x["opener"] = opener_of(x)   # again, now that the engine cards are known
# ---- pass 2: every card of the deck (a complete guide: no card left out) ----
# Each non-handtrap card gets its own hands (the deck sample's + N dealt with it). Its role comes from what the lines did:
# 1-card starter = alone with the deck's 4 most common non-engine cards, the board it builds (field, set cards, GY,
# banished; cards still held don't count) scores >= SOLO: its line is a true 1-card combo (handtraps in hand may serve as
# material or a cost, as in real hands); 2-card starter = in a working pair (two engine cards + non-engine filler build >= SOLO using both); extender = used in >= USED of
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
# Phase A, per card: its hands (pool + top-up), what the lines did with it, and its line alone (1-card test).
cinfo = {}
for st in cards:
    hands = [x for x in sample if st in x["hand"]]
    for i in range(max(0, N - len(hands))):   # top up: most cards already have enough hands in the pool
        h = deal(st); b = search(h)
        if b: x = brief(b, h); x["opener"] = opener_of(x); hands.append(x)
    if not hands: continue
    out.setdefault("hands", {})[str(st)] = [{"hand": x["hand"], "score": x["score"], "uses": x["uses"], "opener": x["opener"], "board": x["field"] + x["backrow"]} for x in hands]   # for later re-analysis
    solo = None
    if brick_hand:
        sh = [st] + [c for c in brick_hand if c != st][:4]
        b = search(sh, stable=STABLE, stableMinMs=1500)
        if b: solo = brief(b, sh); solo["opener"] = opener_of(solo)
    cinfo[st] = {"hands": hands, "solo": solo, "built": built(solo) if solo else 0}
    print("%s: alone builds %.1f, %d hands" % (nm(st), cinfo[st]["built"], len(hands)), flush=True)
# Phase B, pairs: every two engine cards that aren't 1-card starters, with the rest of the hand non-engine. A pair "works"
# when its line builds a real board (>= SOLO, cards left in hand not counted) using both cards: a guide's 2-card combo.
# This decides 2-card starters (instead of how often a card happened to open its hands' lines, which swung 19-50% for
# Ecclesia between runs: what goes first depends on the other cards in the hand).
one = {c for c in cinfo if cinfo[c]["built"] >= SOLO}
rest = [c for c in cards if c in cinfo and c not in one]
pairs_by = defaultdict(list); combos_with = defaultdict(list)
if brick_hand:
    for i, a in enumerate(rest):
        for b2 in rest[i:]:
            if a == b2 and main.count(a) < 2: continue
            ph = [a, b2] + [c for c in brick_hand][:3]
            b = search(ph, stable=STABLE, stableMinMs=1500)
            if not b: continue
            x = brief(b, ph); op = opener_of(x); bl = built(x)
            ok = bl >= SOLO and a in x["useful"] and b2 in x["useful"] and (a != b2 or x["useful"].count(a) >= 2)
            # The pair's starter is the card its line opens with; the other one extends it (Ecclesia + Urgula: Ecclesia
            # starts, Urgula is the extender that combos with it).
            if ok and op in (a, b2):
                other = b2 if op == a else a
                pairs_by[op].append({"partner": other, "score": x["score"], "built": round(bl, 2), "line": x})
                combos_with[other].append(op)
    print("pairs: %d tested, %d cards in a working pair" % (len(rest) * (len(rest) + 1) // 2, len(pairs_by)), flush=True)
# Phase C, roles and the guide entries.
for st in cards:
    if st not in cinfo: continue
    hands, solo, gain = cinfo[st]["hands"], cinfo[st]["solo"], cinfo[st]["built"]
    sc = [x["score"] for x in hands]; med = statistics.median(sc)
    pairs = sorted(pairs_by.get(st, []), key=lambda x: -x["score"])
    opens = sum(1 for x in hands if x["opener"] == st) / len(hands)
    usedr = sum(1 for x in hands if st in x.get("useful", x["uses"])) / len(hands)
    kind = "1-card" if st in one else "2-card" if pairs else None
    role = "starter" if kind else "extender" if usedr >= USED else "situational"
    base = {"card": st, "copies": main.count(st), "role": role, "hands": len(hands), "median": round(med, 2), "opens": round(opens, 2), "used": round(usedr, 2),
            "reliability": round(sum(1 for s in sc if s >= good) / len(sc), 2), "kind": kind, "soloBuilt": round(gain, 2), "solo": solo,
            "soloFiller": [c for c in (solo or {}).get("uses", []) if not engine(c)], "combosWith": sorted(set(combos_with.get(st, [])))}
    users = [x for x in hands if st in x["uses"]]
    if users: base["best"] = max(users, key=lambda x: x["score"])   # its best line (extenders / situational: where it shines)
    out["cards"].append({k: v for k, v in base.items() if k != "best"} | ({"best": base["best"]} if users else {}))
    print("%s: %s%s (alone builds %.1f, %d working pairs, opens %d%%, used %d%%), %d hands, median %.2f, good in %d%%" % (nm(st), role, " " + kind if kind else "", gain, len(pairs), round(opens * 100), round(usedr * 100), len(hands), med, round(base["reliability"] * 100)), flush=True)
    if role != "starter": json.dump(out, open(outf, "w"), indent=1); continue
    # A 1-card starter's main line is its line alone (with non-engine cards): the guide's "1-card combo"; a 2-card
    # starter's is its best working pair, the next 3 pairs listed.
    mainline = solo if kind == "1-card" else pairs[0]["line"]
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
        b = search(mainline["hand"], oppFirst=ht, stable=STABLE, stableMinMs=1500)
        if not b: continue
        hit = any(s["do"] == "Opp" for s in b["steps"])
        # Relative to the main line, capped at it: a separate search can land a little higher by chance.
        keep = min(1.0, b["score"] / mainline["score"]) if mainline["score"] > 0 else 0
        vs.append({"by": ht, "used": hit, "score": round(min(b["score"], mainline["score"]), 2), "keeps": round(keep, 2) if hit else 1.0, "line": brief(b, mainline["hand"]) if hit else None})
    entry = dict(base, main=mainline, upgrades=ups[:3], vs=vs, pairs=[{"partner": x["partner"], "score": x["score"], "line": x["line"]} for x in pairs[1:4]]); entry.pop("best", None)
    out["starters"].append(entry); json.dump(out, open(outf, "w"), indent=1)   # saved after each card
    print("  main %.2f (uses %s), upgrades %s, vs %s" % (mainline["score"], ", ".join(nm(c) for c in mainline["uses"]), ", ".join("%s +%.1f" % (nm(u["card"]), u["gain"]) for u in ups[:3]) or "-",
          " ".join("%s %d%%%s" % (HT[v["by"]].split()[0], round(v["keeps"] * 100), "" if v["used"] else "(n/a)") for v in vs)), flush=True)
# How often each card ends on the board without having been in the opening hand (Dracotail Traps Set from the Deck by
# other cards): over every searched hand, once per distinct hand.
seen = {}
for xs in out.get("hands", {}).values():
    for x in xs: seen[tuple(sorted(x["hand"]))] = x
for c in out["cards"]:
    pool = [x for x in seen.values() if c["card"] not in x["hand"]]
    c["fromDeck"] = round(sum(1 for x in pool if c["card"] in x["board"]) / len(pool), 2) if pool else 0
json.dump(out, open(outf, "w"), indent=1)
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
print("wrote", outf, "in %.1f min" % ((time.time() - T0) / 60))
