"""Guide vs engine: for each guide combo in a refs file, search from its starting hand and compare the engine's best
board with the guide's end board, both scored by the engine.
Usage: python3 compare.py <engine> <cards.cdb> <scripts.zip> <refs.json> <secs> [filler card] [only: name substring]
Generic hand slots ("<Any>", "<Lv4 Monster>"...) become the filler card (default Nibiru, the Primal Being: a card the
combo won't use); boards whose start names alternatives ("Lukias / Ketu") are skipped unless spelled out.
Env: THREADS (default: all cores), ACTS (max actions, default 40)."""
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
qid = 10
for b in refs["boards"]:
    if only and only not in b["name"].lower(): continue
    hand = []
    for s in b["start"]:
        if s.startswith("<"):   # only truly generic slots take the filler; "<Spellcaster>" needs a real card
            if s.strip("<>").lower().split("(")[0].strip() in ("any", "any card", "any monster", "discard"): hand.append(code(filler)); continue
            hand = None; break
        if code(s) is None: hand = None; break
        hand.append(code(s))
    if not hand: print("\n## %s: skipped (start %s)" % (b["name"], b["start"])); continue
    main = list(deck["main"])
    for c in hand:   # the hand comes out of the Deck
        if c in main: main.remove(c)
    qid += 1
    r = ask({"id": qid, "cmd": "search", "deck": main, "extra": deck["extra"], "hand": hand, "timeMs": int(float(secs) * 1000), "threads": threads, "top": 3, "maxActions": acts})
    qid += 1
    q = {"id": qid, "cmd": "score", "extra": deck["extra"], "targets": []}
    for k in ("field", "backrow", "hand", "gy"): q[k] = [code(x) for x in b.get(k, []) if code(x)]
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
        print("   -> %s" % verdict)
    else: print("   engine: no boards (%s)" % r.get("error", ""))
    if os.environ.get("WHY"):
        for x in g.get("stops", []): print("      guide stop %5.2f  %s" % (x["value"], x["what"]))
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
