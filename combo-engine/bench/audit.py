"""Deck audit: where is the engine likely wrong on a deck nobody has written a guide for?
Runs the deck's starting hands through the engine and reports, without any guide to compare against:
  1. card reading   cards whose text looks like it does something on the opponent's turn, but the board score reads
                    as worth nothing (likely misreads in src/evaluate.h)
  2. search limits  hands where a long search finds a much better board than a short one (the search, not the scoring,
                    is the problem there; generation uses short searches)
  3. unused cards   main deck cards that never appear in any best line (a misread, a search miss, or a dead card)
  4. second opinion the opponent's-turn simulation against each best board: cards the score counts as interruptions that
                    never stopped anything (overrated), and cards that stopped plays the score ignores (underrated)
Usage: python3 audit.py <engine> <cards.cdb> <scripts.zip> <deck> [short secs=4] [long secs=30]
  <deck>: a deck JSON {"main":[codes],"extra":[codes]}, a .ydk file, or cloud:<deck id> (a shared deck; needs
  BENCH_USER / BENCH_PASS, the enginetest account).
Env: THREADS, ACTS (max actions, 40), PAIRS (how many of the best single cards to pair up, 5; 0 = singles only),
  TARGETS (goal card codes, comma-separated; default: the deck's own goals for cloud decks), OUT (report JSON path)."""
import json, os, sqlite3, subprocess, sys, time, urllib.request
from collections import defaultdict
exe, cdb, scripts, src = sys.argv[1:5]
short_s = float(sys.argv[5]) if len(sys.argv) > 5 else 4.0
long_s = float(sys.argv[6]) if len(sys.argv) > 6 else 30.0
threads = int(os.environ.get("THREADS") or os.cpu_count()); acts = int(os.environ.get("ACTS") or 40); npairs = int(os.environ.get("PAIRS") or 5)
db = sqlite3.connect(cdb)
def row(c): return db.execute("select t.name, t.desc, d.type from datas d join texts t on t.id = d.id where d.id=?", (c,)).fetchone()
def n(c): r = row(c); return r[0] if r else str(c)

# ---- the deck ----
targets = [int(x) for x in os.environ.get("TARGETS", "").split(",") if x.strip()]
name = os.path.basename(src)
if src.startswith("cloud:"):
    U = "https://hopdbvllzumhywaxerno.supabase.co"; K = "sb_publishable_F6Q8lBvzyy4x71O-CVg81g_WkFVjyY8"
    def call(path, data=None, tok=None):
        h = {"apikey": K, "Content-Type": "application/json"}
        if tok: h["Authorization"] = "Bearer " + tok
        r = urllib.request.Request(U + path, data=json.dumps(data).encode() if data is not None else None, headers=h)
        b = urllib.request.urlopen(r).read(); return json.loads(b) if b else None
    tok = call("/auth/v1/token?grant_type=password", {"email": os.environ["BENCH_USER"].strip().lower() + "@players.mdc-app.com", "password": os.environ["BENCH_PASS"].strip()})["access_token"]
    rows = call("/rest/v1/decks?select=name,data&id=eq." + src[6:], tok=tok)
    if not rows: sys.exit("no such shared deck")
    d = rows[0]["data"]; name = rows[0]["name"]
    main = [r["id"] for r in d["main"] for _ in range(r["n"])]; extra = [r["id"] for r in d.get("extra", []) for _ in range(r["n"])]
    if not targets: targets = d.get("goals") or []
elif src.endswith(".ydk"):
    main, extra, sec = [], [], "main"
    for line in open(src, encoding="utf-8", errors="ignore"):
        line = line.strip()
        if line.startswith("#extra"): sec = "extra"
        elif line.startswith("!side"): sec = "side"
        elif line.startswith("#main"): sec = "main"
        elif line.isdigit() and sec != "side": (main if sec == "main" else extra).append(int(line))
else:
    d = json.load(open(src)); main, extra = list(d["main"]), list(d["extra"])

p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready") or "score" in r or "eval" in r): return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": scripts})
uniq = list(dict.fromkeys(main))
ev = ask({"id": 2, "cmd": "eval", "cards": sorted(set(main + extra))})["eval"]
report = {"deck": name, "main": len(main), "extra": len(extra), "targets": targets}
out = []
def say(s=""): out.append(s); print(s, flush=True)
say("# Audit: %s (%d main, %d extra)%s" % (name, len(main), len(extra), "  goals: " + ", ".join(n(c) for c in targets) if targets else ""))

# ---- 1. card reading ----
MARK = ["(quick effect)", "opponent's turn", "your opponent activates", "either player's turn", "when your opponent", "your opponent would",
        "opponent normal or special summons", "opponent special summons", "in response"]
def worth(e): return max(e.get(k, 0) for k in ("field", "set", "hand", "gy", "lock")) + (1 if e.get("revive") or e.get("fusion") or e.get("brings") else 0)
silent = []
for c in sorted(set(main + extra)):
    r = row(c); e = ev.get(str(c))
    if not r or not e: continue
    t = r[1].lower(); typ = r[2]
    trapish = bool(typ & 0x4) or (typ & 0x2 and typ & 0x10000)   # Traps and Quick-Plays are played on their turn
    if (trapish or any(m in t for m in MARK)) and worth(e) == 0 and not e.get("onSummon"):
        silent.append(c)
say("\n## 1. Card reading: does something on their turn, scored as nothing (%d)" % len(silent))
say("   (check each against its text; some are fine: costs, searches, our-turn-only Quick-Plays)")
for c in silent: say("   - %s" % n(c))
report["silent"] = [n(c) for c in silent]

# ---- 2. lines: short vs long search ----
def search(hand, secs, sim):
    deck = list(main)
    for c in hand:
        if c in deck: deck.remove(c)
    q = {"id": int(time.time() * 1000) % 1000000000, "cmd": "search", "deck": deck, "extra": extra, "hand": hand, "timeMs": int(secs * 1000),
         "threads": threads, "top": 6 if sim else 1, "maxActions": acts, "targets": targets, "sim": sim}
    return ask(q)
hands = [[c] for c in uniq]
res = {}
say("\n## 2. Search: short (%gs, like generation) vs long (%gs)" % (short_s, long_s))
def run(h):
    a = search(h, short_s, False); b = search(h, long_s, True)
    sa = a["boards"][0]["score"] if a.get("boards") else 0; bb = b.get("boards") or []
    sb = max([x["score"] if not x.get("sim", {}).get("ok") else x["sim"].get("textScore", x["score"]) for x in bb] or [0])
    res[tuple(h)] = {"short": sa, "long": sb, "boards": bb, "complete": b.get("complete"), "bestAt": b.get("stats", {}).get("bestAt")}
    flag = "  <-- search-limited" if sb - sa > 1.0 else ""
    say("   %-55s %6.2f -> %6.2f%s%s" % (" + ".join(n(c) for c in h)[:55], sa, sb, "  (every line checked)" if b.get("complete") else "", flag))
for h in hands: run(h)
if npairs:
    # pair up cards that make plays on their own (their best line has real steps), not handtraps sitting in hand
    plays = lambda c: len((res[(c,)]["boards"] or [{}])[0].get("steps", []))
    best = sorted([h[0] for h in hands if plays(h[0]) >= 2], key=lambda c: -res[(c,)]["long"])[:npairs]
    for i in range(len(best)):
        for j in range(i + 1, len(best)): run([best[i], best[j]])
lim = [k for k, v in res.items() if v["long"] - v["short"] > 1.0]
report["hands"] = {" + ".join(n(c) for c in k): {"short": v["short"], "long": v["long"], "complete": v["complete"]} for k, v in res.items()}
report["searchLimited"] = [" + ".join(n(c) for c in k) for k in lim]

# ---- 3. unused cards ----
used = set()
for v in res.values():
    for bb in v["boards"][:1]:
        for s in bb.get("steps", []): used.add(s.get("card"))
        for z in ("field", "backrow", "gy", "banished"): used.update(bb.get(z, []))
unused = [c for c in uniq if c not in used and not ev.get(str(c), {}).get("hand") and not ev.get(str(c), {}).get("set")]   # handtraps / set interruptions wait for their turn
say("\n## 3. Main deck cards in no best line (handtraps and set interruptions left out) (%d)" % len(unused))
for c in unused: say("   - %s" % n(c))
report["unused"] = [n(c) for c in unused]

# ---- 4. second opinion: the opponent's-turn simulation ----
T = defaultdict(lambda: {"boards": 0, "text": 0.0, "credited": 0, "unused": 0, "dead": 0, "stopsNoText": 0})
qid = 5000
for v in res.values():
    for bb in v["boards"]:
        s = bb.get("sim", {})
        if not s.get("ok"): continue
        qid += 1
        why = ask({"id": qid, "cmd": "score", "field": bb["field"], "zones": bb.get("zones", []), "backrow": bb["backrow"], "hand": bb["hand"], "gy": bb.get("gy", []),
                   "banished": bb.get("banished", []), "extra": extra, "targets": targets, "deck": main})["score"]
        text = defaultdict(float)
        for st in why.get("stops", []):
            if st.get("code"): text[st["code"]] = max(text[st["code"]], st["value"])
        # A monster made on their turn (Faimena's Mirrorjade, Ecclesia's Fallen of Albaz) is credited by the simulation as
        # itself; the score counts it under the card that made it ("fusion into X", "... -> X"): credit that card.
        onboard = set(bb["field"] + bb["backrow"] + bb["hand"] + bb.get("gy", []) + bb.get("banished", []))
        maker = {}
        for st in why.get("stops", []):
            w = st.get("what", "")
            for key in ("fusion into ", " -> "):
                if key in w and st.get("code"): maker[w.split(key, 1)[1].strip().lower()] = st["code"]
        cred = defaultdict(float)
        for cr in s.get("credits", []) + s.get("breaker", {}).get("credits", []):
            c = cr.get("card")
            if not c: continue
            if c not in onboard:   # made during their turn: credit the card that made it (by name if the score made the same, else every maker)
                if n(c).lower() in maker: c = maker[n(c).lower()]
                elif maker:
                    for mc in set(maker.values()): cred[mc] = max(cred[mc], cr["value"])
                    continue
            cred[c] = max(cred[c], cr["value"])
        unt = {u["card"]: u["value"] for u in s.get("untested", [])}
        for c, val in text.items():
            t = T[c]; t["boards"] += 1; t["text"] += val
            if c in cred: t["credited"] += 1
            elif c in unt:   # never got the chance: dead if its condition wasn't met, else no verdict
                if unt[c] <= 0: t["dead"] += 1
            else: t["unused"] += 1   # had the chance, stopped nothing
        for c in cred:
            if text.get(c, 0) <= 0.5: T[c]["stopsNoText"] += 1
over = [(c, t) for c, t in T.items() if t["credited"] == 0 and t["unused"] + t["dead"] >= 2 and t["text"] / t["boards"] >= 1.0]
under = [(c, t) for c, t in T.items() if t["stopsNoText"] >= 2]
say("\n## 4. Opponent's-turn simulation disagrees")
say("   Overrated (scored as an interruption, never stopped anything in play):")
for c, t in sorted(over, key=lambda kv: -kv[1]["boards"]): say("   - %-45s on %d boards, scored %.1f" % (n(c)[:45], t["boards"], t["text"] / t["boards"]))
if not over: say("   (none)")
say("   Underrated (stopped plays; the score gives it <= 0.5):")
for c, t in sorted(under, key=lambda kv: -kv[1]["stopsNoText"]): say("   - %-45s stopped plays on %d boards" % (n(c)[:45], t["stopsNoText"]))
if not under: say("   (none)")
report["overrated"] = [n(c) for c, _ in over]; report["underrated"] = [n(c) for c, _ in under]
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
if os.environ.get("OUT"): json.dump(report, open(os.environ["OUT"], "w"), indent=1)
