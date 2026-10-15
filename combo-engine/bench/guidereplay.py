"""Can the engine play a guide's line at all? A guided search follows the guide's acting cards in order (request
"guide"): only the next card may act on our free choices and in optional chain windows; picks, zones and targets stay
free. Prints how far into the sequence the engine got (where it got stuck, if it did) and the board it ends on.
  python3 guidereplay.py <engine> <cdb> <zip> <deck.json> <cached MDM combo .json> [secs] [hand names, comma-separated]
Acting cards from the guide's steps: a step's Link Summon = the Link monster (materials come first); "effect" /
"graveyard" entries and "activate" = that card; a summon that opens a step (Combined Creation, Horned Dragon) = that card;
later summons / adds / sends are results; Sets and End Phase entries are skipped (play is free after the sequence)."""
import json, os, sqlite3, subprocess, sys
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf, deckf, combof = sys.argv[1:6]
secs = float(sys.argv[6]) if len(sys.argv) > 6 else 60
db = sqlite3.connect(cdb); d = json.load(open(deckf, encoding="utf-8"))
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0]
pool = sorted(set(d["main"]) | set(d["extra"]))
def code(name):
    for c in pool:
        if nm(c).lower() == name.lower(): return c
    r = db.execute("select id from texts where lower(name)=? order by id", (name.lower(),)).fetchone()
    return r[0] if r else None
c = json.load(open(combof, encoding="utf-8")); c = c if isinstance(c, dict) else c[0]
actors, picks, soft = [], [], []   # picks[i]: the other cards named in acting card i's part of its step
PROC = ("normal summon", "link summon", "xyz summon", "synchro summon", "tribute summon")
for s in c["steps"]:
    es = [e for e in s["entries"] if e.get("card")]
    if not es: continue
    labs = [(e.get("label") or "").lower() for e in es]
    syms = [e.get("symbol") or "" for e in es]
    cons = [e.get("connector") or "" for e in es]
    # A material summon: cards joined by plus / equals (old "link" label, or no label at all): the result is the actor.
    if labs[0] == "link" or (not syms[0] and not labs[0] and ("plus" in cons or "equals" in cons)):
        k = next((i + 1 for i, cn in enumerate(cons) if cn == "equals" and i + 1 < len(es)), None)
        if k is None: k = next((i for i in range(len(es) - 1, 0, -1) if syms[i] == "summon"), len(es) - 1)
        actors.append(es[k]["card"]["name"]); picks.append([e["card"]["name"] for i, e in enumerate(es) if i != k]); soft.append(0)
        for e in es[k + 1:]:   # anything after the summon in the same step (its effect)
            if (e.get("symbol") or "") in ("effect", "chain") or any(w in (e.get("label") or "").lower() for w in ("effect", "activate")): actors.append(e["card"]["name"]); picks.append([]); soft.append(0)
            elif picks: picks[-1].append(e["card"]["name"])
        continue
    for i, e in enumerate(es):
        sym, lab = syms[i], labs[i]
        if sym in ("set_st",) or "endphase" in lab or "end phase" in lab: continue
        hard = any(w in lab for w in PROC)
        act = sym in ("effect", "chain") or "effect" in lab or "activate" in lab
        sft = i == 0 and sym == "summon" and not hard
        if hard or act or sft:
            actors.append(e["card"]["name"]); picks.append([]); soft.append(1 if sft and not act else 0)
        elif picks: picks[-1].append(e["card"]["name"])
codes = [code(a) for a in actors]
missing = [a for a, x in zip(actors, codes) if not x]
if missing: print("not in the card database:", missing)
seq = [x for x in codes if x]
if len(sys.argv) > 7: hand = [code(h.strip()) for h in sys.argv[7].split(",")]
else: hand = [h for h in (code(x["name"]) for x in c.get("startingCards", []) if isinstance(x, dict) and x.get("name")) if h] or [seq[0]]
deck = list(d["main"])
for h in hand:
    if h in deck: deck.remove(h)
print("guide: %s | hand: %s" % (c.get("title"), ", ".join(nm(h) for h in hand)))
print("acting sequence (%d): %s" % (len(seq), " > ".join(nm(x) for x in seq)))
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1); q = [0]
def ask(o):
    q[0] += 1; o["id"] = q[0]; p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("ready") or r.get("done") or r.get("error")): return r
ask({"cmd": "init", "cdb": cdb, "scripts": zipf})
gp = [[code(x) for x in pk if code(x)] for pk, cc in zip(picks, codes) if cc]
req = {"cmd": "search", "deck": deck, "extra": d["extra"], "hand": hand, "timeMs": int(secs * 1000), "threads": int(os.environ.get("THREADS") or 12), "top": 5, "maxActions": 60, "guide": seq, "labels": True}
if not os.environ.get("NOPICKS"): req["guidePicks"] = gp   # NOPICKS=1: only the acting cards
req["guideSoft"] = [sf for sf, cc in zip(soft, codes) if cc]
r = ask(req)
best_j, best_b = -1, None
for b in r.get("boards", []):
    j = 0
    for s in b["steps"]:
        if j < len(seq) and s["card"] == seq[j] and s["do"] in ("Activate", "Use", "Normal Summon", "Special Summon"): j += 1
    if j > best_j or (j == best_j and best_b and b["score"] > best_b["score"]): best_j, best_b = j, b
if not best_b: print("no board"); sys.exit()
print("followed %d of %d acting cards%s" % (best_j, len(seq), "" if best_j >= len(seq) else "; stuck before: %s (#%d)" % (nm(seq[best_j]), best_j + 1)))
print("board %.2f | field: %s | backrow: %s | hand: %s" % (best_b["score"], ", ".join(nm(x) for x in best_b["field"]), ", ".join(nm(x) for x in best_b["backrow"]), ", ".join(nm(x) for x in best_b["hand"])))
if os.environ.get("STEPS"):
    for s in best_b["steps"][-int(os.environ["STEPS"]):]: print("   %s | %s | %s | %s" % (s["do"], nm(s["card"]), (s.get("effect") or "")[:60], ", ".join(nm(x) for x in s.get("picks", []))))
eb = c.get("endBoard") or []
if eb: print("guide end board: %s" % ", ".join(x["name"] if isinstance(x, dict) and x.get("name") else str(x) for x in eb)[:300])
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
