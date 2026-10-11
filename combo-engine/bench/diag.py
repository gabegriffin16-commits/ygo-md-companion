"""Where does the text model misread cards? Runs every guide hand of the given refs files with the opponent's-turn
simulation on and tallies, per card, how its text value compares with what it did when their turn was played out:
  overrated   the text model counts it as an interruption, but it never stopped anything (had the chance and didn't,
              or was never even offered: its condition wasn't met)
  underrated  it stopped plays in the simulation, but the text model gives it little or nothing
Usage: python3 diag.py <engine> <cards.cdb> <scripts.zip> <secs> <refs.json>...   (env THREADS, ACTS, TOP)"""
import json, os, re, sqlite3, subprocess, sys
from collections import defaultdict
exe, cdb, scripts, secs = sys.argv[1:5]; refsfs = sys.argv[5:]
db = sqlite3.connect(cdb)
names = {}
for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id = d.id order by (d.alias != 0), d.id"): names.setdefault(nm.lower(), c)
def n(c): r = db.execute("select name from texts where id=?", (c,)).fetchone(); return r[0] if r else str(c)
def code(x): return int(x) if str(x).isdigit() else names.get(str(x).lower())
def subseq(a, b): it = iter(b); return all(ch in it for ch in a)
def fits(slot, c):
    r = db.execute("select t.name, d.level, d.type from datas d join texts t on t.id = d.id where d.id=?", (c,)).fetchone()
    if not r: return False
    nm, lv, typ = r[0].lower(), r[1] & 0xff, r[2]
    for w in re.findall(r"[a-z0-9]+", slot):
        if w in ("name", "card", "cards", "any"): continue
        if w in ("tuner", "tuners"):
            if not typ & 0x1000: return False
        elif w in ("monster", "monsters"):
            if not typ & 1: return False
        elif re.fullmatch(r"(lv|level)(\d+)", w):
            if not typ & 1 or lv != int(re.fullmatch(r"(lv|level)(\d+)", w).group(2)): return False
        elif not any(subseq(w, x) for x in re.findall(r"[a-z0-9]+", nm)): return False
    return True
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready") or "score" in r): return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": scripts})
threads = int(os.environ.get("THREADS") or os.cpu_count()); acts = int(os.environ.get("ACTS") or 40); top = int(os.environ.get("TOP") or 6)
T = defaultdict(lambda: {"boards": 0, "text": 0.0, "credited": 0, "credit": 0.0, "unused": 0, "dead": 0, "half": 0, "stopsNoText": 0})
qid = 10; hands = 0
for rf in refsfs:
    refs = json.load(open(rf, encoding="utf-8"))
    dp = refs["deck"] if os.path.exists(refs["deck"]) else os.path.join(os.path.dirname(rf), "..", refs["deck"])
    deck = json.load(open(dp))
    seen = set()
    for b in refs["boards"]:
        hand = []
        for s in b["start"]:
            if s.startswith("<"):
                slot = s.strip("<>").lower().split("(")[0].strip()
                if slot in ("any", "any card", "any monster", "discard"): hand.append(code("Nibiru, the Primal Being")); continue
                cand = [c for c in dict.fromkeys(deck["main"]) if c not in hand and fits(slot, c)]
                if not cand: hand = None; break
                hand.append(max(cand, key=lambda c: deck["main"].count(c))); continue
            if code(s) is None: hand = None; break
            hand.append(code(s))
        if not hand or tuple(sorted(hand)) in seen: continue
        seen.add(tuple(sorted(hand))); hands += 1
        main = list(deck["main"])
        for c in hand:
            if c in main: main.remove(c)
        qid += 1
        r = ask({"id": qid, "cmd": "search", "deck": main, "extra": deck["extra"], "hand": hand, "timeMs": int(float(secs) * 1000), "threads": threads, "top": top, "maxActions": acts, "sim": True})
        for bb in r.get("boards", []):
            s = bb.get("sim", {})
            if not s.get("ok"): continue
            qid += 1
            why = ask({"id": qid, "cmd": "score", "field": bb["field"], "zones": bb.get("zones", []), "backrow": bb["backrow"], "hand": bb["hand"], "gy": bb.get("gy", []), "banished": bb.get("banished", []), "extra": deck["extra"]})["score"]
            text = defaultdict(float)
            for st in why.get("stops", []):
                if st.get("code"): text[st["code"]] = max(text[st["code"]], st["value"])
            cred = defaultdict(float)
            for cr in s.get("credits", []) + s.get("breaker", {}).get("credits", []):
                if cr.get("card"): cred[cr["card"]] = max(cred[cr["card"]], cr["value"])
            unt = {u["card"]: u["value"] for u in s.get("untested", [])}
            for c, v in text.items():
                t = T[c]; t["boards"] += 1; t["text"] += v
                if c in cred: t["credited"] += 1; t["credit"] += cred[c]
                elif c in unt: t["half" if unt[c] > 0 else "dead"] += 1
                else: t["unused"] += 1
            for c, v in cred.items():
                if text.get(c, 0) <= 0.5: T[c]["stopsNoText"] += 1
        print("  %s: %d boards" % (" + ".join(n(c) for c in hand), len(r.get("boards", []))), file=sys.stderr, flush=True)
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
print("%d hands\n\nOVERRATED (the text model counts it; in play it never stopped anything)" % hands)
rows = sorted(T.items(), key=lambda kv: -(kv[1]["unused"] + kv[1]["dead"]) * (kv[1]["text"] / max(1, kv[1]["boards"])))
print("%-44s %6s %8s %8s %8s %8s %6s" % ("card", "boards", "avgText", "stopped", "hadChance", "neverOff", "half"))
for c, t in rows[:25]:
    if t["unused"] + t["dead"] == 0: continue
    print("%-44s %6d %8.2f %8d %8d %8d %6d" % (n(c)[:44], t["boards"], t["text"] / max(1, t["boards"]), t["credited"], t["unused"], t["dead"], t["half"]))
print("\nUNDERRATED (stopped plays in the simulation; the text model gives it <= 0.5)")
for c, t in sorted(T.items(), key=lambda kv: -kv[1]["stopsNoText"])[:15]:
    if t["stopsNoText"]: print("%-44s stopped plays on %d boards" % (n(c)[:44], t["stopsNoText"]))
