"""Guide vs engine: for each guide combo in a refs file, search from its starting hand and compare the engine's best
board with the guide's end board, both scored by the engine.
Usage: python3 compare.py <engine> <cards.cdb> <scripts.zip> <refs.json> <secs> [filler card] [only: name substring]
Generic hand slots ("<Any>") become the filler card (default Nibiru, the Primal Being: a card the combo won't use). Typed
slots ("<Dtail name>", "<Lv4 Monster>", "<Urgula / Pan>") become the deck card that fits (name words, abbreviations as
letters in order, Level), the one with the most copies. Boards whose guide uses cards the deck lacks are marked.
Env: THREADS (default: all cores), ACTS (max actions, default 40), SIM=1 (play the opponent's turn against the best
boards and rank by that; also prints whether the text-ranked and the simulation-ranked #1 hold the guide's field)."""
import json, os, sqlite3, subprocess, sys
exe, cdb, scripts, refsf, secs = sys.argv[1:6]
filler = sys.argv[6] if len(sys.argv) > 6 and sys.argv[6] else "Nibiru, the Primal Being"
only = sys.argv[7].lower() if len(sys.argv) > 7 else ""
db = sqlite3.connect(cdb)
names = {}
for code, name in db.execute("select d.id, t.name from datas d join texts t on t.id = d.id order by (d.alias != 0), d.id"):
    names.setdefault(name.lower(), code)
def n(c):
    r = db.execute("select name from texts where id=?", (c,)).fetchone(); return r[0] if r else str(c)
def code(x): return int(x) if str(x).isdigit() else names.get(str(x).lower())
import re
def subseq(a, b): it = iter(b); return all(ch in it for ch in a)
RACES = ["warrior", "spellcaster", "fairy", "fiend", "zombie", "machine", "aqua", "pyro", "rock", "winged beast", "plant", "insect", "thunder",
         "dragon", "beast", "beast-warrior", "dinosaur", "fish", "sea serpent", "reptile", "psychic", "divine-beast", "creator god", "wyrm", "cyberse", "illusion"]
def race_of(slot):
    """A slot that names a monster Type ("Spellcaster", "Winged") -> its race bit."""
    w = slot.replace(" monster", "").strip()
    for i, r in enumerate(RACES):
        if r == w: return 1 << i
    for i, r in enumerate(RACES):
        if len(w) >= 4 and r.startswith(w): return 1 << i
    return 0
def fits(slot, c):
    """Does deck card c fit a typed slot like "Dtail name", "Lv4 Monster" or "Spellcaster"?"""
    r = db.execute("select t.name, d.level, d.type, d.race from datas d join texts t on t.id = d.id where d.id=?", (c,)).fetchone()
    if not r: return False
    nm, lv, typ = r[0].lower(), r[1] & 0xff, r[2]
    if race_of(slot): return bool(typ & 1 and r[3] & race_of(slot))
    for w in re.findall(r"[a-z0-9]+", slot):
        if w in ("name", "card", "cards", "any"): continue
        if w in ("tuner", "tuners"):
            if not typ & 0x1000: return False
            continue
        if w in ("monster", "monsters"):
            if not typ & 1: return False
        elif w in ("spell", "trap"):
            if not typ & (2 if w == "spell" else 4): return False
        elif re.fullmatch(r"(lv|level)(\d+)", w):
            if not typ & 1 or lv != int(re.fullmatch(r"(lv|level)(\d+)", w).group(2)): return False
        elif not any(subseq(w, x) for x in re.findall(r"[a-z0-9]+", nm)): return False
    return True
def handtrap(c):
    d = (db.execute("select desc from texts where id=?", (c,)).fetchone() or [""])[0].lower()
    return "(quick effect)" in d and any(x in d for x in ("discard this card", "send this card from your hand", "from your hand to the gy", "this card in your hand"))
def pick(slot, main, hand):
    """The deck card for a typed slot (alternatives split on "/"), or None."""
    left = list(main)
    for c in hand:
        if c in left: left.remove(c)
    for alt in re.split(r"[/,]", slot):
        cands = [c for c in dict.fromkeys(left) if fits(alt.strip(), c)]
        if cands: return max(cands, key=lambda c: (not handtrap(c), left.count(c)))   # a card the line uses, not a handtrap
    return None
refs = json.load(open(refsf, encoding="utf-8"))
deck = json.load(open(os.path.join(os.path.dirname(refsf), "..", refs["deck"]) if not os.path.isabs(refs["deck"]) and not os.path.exists(refs["deck"]) else refs["deck"]))
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready") or "score" in r): return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": scripts})
threads = int(os.environ.get("THREADS") or os.cpu_count()); acts = int(os.environ.get("ACTS") or 40)
SIM = bool(os.environ.get("SIM")); tally = {"hands": 0, "text": 0, "sim": 0}
qid = 10
for b in refs["boards"]:
    if only and only not in b["name"].lower(): continue
    hand = []
    for s in b["start"]:
        if s.startswith("<"):   # only truly generic slots take the filler; typed ones need a fitting card
            slot = s.strip("<>").lower().split("(")[0].strip()
            if slot in ("any", "any card", "any monster", "discard"): hand.append(code(filler)); continue
            c = pick(slot, deck["main"], hand)
            if c is None: hand = None; break
            hand.append(c); continue
        if code(s) is None: hand = None; break
        hand.append(code(s))
    if not hand: print("\n## %s: skipped (start %s)" % (b["name"], b["start"])); continue
    main = list(deck["main"])
    for c in hand:   # the hand comes out of the Deck
        if c in main: main.remove(c)
    qid += 1
    r = ask({"id": qid, "cmd": "search", "deck": main, "extra": deck["extra"], "hand": hand, "timeMs": int(float(secs) * 1000), "threads": threads, "top": 12 if SIM else 3, "maxActions": acts, "sim": SIM})
    qid += 1
    q = {"id": qid, "cmd": "score", "extra": deck["extra"], "targets": []}
    for k in ("field", "backrow", "hand", "gy", "banished"): q[k] = [code(x) for x in b.get(k, []) if code(x)]
    g = ask(q)["score"]
    best = r["boards"][0] if r.get("boards") else None
    print("\n## %s  (start: %s)" % (b["name"], " + ".join(n(c) for c in hand)))
    print("   guide  %6.2f  field: %s | backrow: %s | hand: %s" % (g.get("total", 0), ", ".join(b["field"]), ", ".join(b["backrow"]), ", ".join(b.get("hand", []))))
    if best:
        st = r.get("stats", {})
        print("   engine %6.2f  field: %s | backrow: %s | hand: %s   [%d steps, best at %.0fs of %.0fs%s]" % (best["score"], ", ".join(n(c) for c in best["field"]), ", ".join(n(c) for c in best["backrow"]), ", ".join(n(c) for c in best["hand"]),
              len(best["steps"]), st.get("bestAt", 0), st.get("seconds", 0), ", stable" if r.get("stable") else ""))
        gf = sorted(code(x) for x in b["field"] if code(x)); found = [bb for bb in r["boards"] if all(c in bb["field"] for c in gf)]
        verdict = "engine found the guide's field" if found else "engine better by score" if best["score"] > g.get("total", 0) + 0.3 else "guide better: engine missed it" if g.get("total", 0) > best["score"] + 0.3 else "about even"
        if b.get("missing"): verdict += "  [guide uses cards not in this deck: %s]" % ", ".join(b["missing"])
        if b.get("guessed"): verdict += "  [placement guessed: %s]" % ", ".join(b["guessed"])
        print("   -> %s" % verdict)
        if SIM and r["boards"]:
            has = lambda bb: all(c in bb["field"] for c in gf)
            tb = max(r["boards"], key=lambda bb: bb.get("sim", {}).get("textScore", bb["score"]))
            sb = r["boards"][0]
            tally["hands"] += 1; tally["text"] += has(tb); tally["sim"] += has(sb)
            print("   #1 by text (%.2f): %s%s" % (tb.get("sim", {}).get("textScore", tb["score"]), ", ".join(n(c) for c in tb["field"]), "  <- guide's field" if has(tb) else ""))
            fails = sum(1 for bb in r["boards"] if not bb.get("sim", {}).get("ok")); tally["failed"] = tally.get("failed", 0) + fails; tally["boards"] = tally.get("boards", 0) + len(r["boards"])
            print("   #1 by sim  (%.2f): %s%s%s" % (sb["score"], ", ".join(n(c) for c in sb["field"]), "  <- guide's field" if has(sb) else "", "  [%d of %d simulations failed]" % (fails, len(r["boards"])) if fails else ""))
    else: print("   engine: no boards (%s)" % r.get("error", ""))
    if os.environ.get("WHY"):
        for x in g.get("stops", []): print("      guide stop %5.2f  %s" % (x["value"], x["what"]))
if SIM: print("\n#1 holds the guide's field: text %d / sim %d of %d hands (simulations failed: %d of %d boards)" % (tally["text"], tally["sim"], tally["hands"], tally.get("failed", 0), tally.get("boards", 0)))
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
