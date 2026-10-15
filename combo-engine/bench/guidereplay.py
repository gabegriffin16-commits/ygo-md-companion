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
actors, picks = [], []   # picks[i]: the other cards named in acting card i's part of its step (materials, sends, targets)
for s in c["steps"]:
    es = s["entries"]
    if not es: continue
    if (es[0].get("label") or "").lower() == "link":   # materials, then the Link monster
        summ = [e for e in es if e.get("symbol") == "summon" and e.get("card")]
        if summ: actors.append(summ[0]["card"]["name"]); picks.append([e["card"]["name"] for e in es if e.get("card") and e is not summ[0]])
        continue
    for i, e in enumerate(es):
        if not e.get("card"): continue
        sym, lab = e.get("symbol") or "", (e.get("label") or "").lower()
        if sym in ("set_st",) or "endphase" in lab: continue
        if sym in ("effect", "graveyard") or lab.startswith(("activate", "effect", "grave effect")) or (i == 0 and sym == "summon"):
            actors.append(e["card"]["name"]); picks.append([])
        elif picks: picks[-1].append(e["card"]["name"])
codes = [code(a) for a in actors]
missing = [a for a, x in zip(actors, codes) if not x]
if missing: print("not in the card database:", missing)
seq = [x for x in codes if x]
if len(sys.argv) > 7: hand = [code(h.strip()) for h in sys.argv[7].split(",")]
else: hand = [code(x["name"]) for x in c.get("startingCards", []) if isinstance(x, dict) and x.get("name")] or [seq[0]]
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
