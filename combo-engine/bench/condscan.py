"""Whole-database scan of conditions on interruptions: for every card the text model credits with an interruption
(>= 1.5 in some place), the condition in front of its interrupting effect (the text before the ":"), grouped by its
opening words and counted, with whether the engine already checks it (`needs`). Shows which kinds of condition are
worth modeling first.
  python3 condscan.py <engine> <cards.cdb> <scripts.zip> [examples per group, 3]"""
import json, re, sqlite3, subprocess, sys
from collections import defaultdict
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf = sys.argv[1:4]; nex = int(sys.argv[4]) if len(sys.argv) > 4 else 3
db = sqlite3.connect(cdb)
ids = [r[0] for r in db.execute("select d.id from datas d join texts t on t.id=d.id where d.alias=0 and d.ot & 3 and d.type & 0x4000 = 0 and d.id < 100000000")]
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
o = p.communicate(json.dumps({"id": 1, "cmd": "init", "cdb": cdb, "scripts": zipf}) + "\n" + json.dumps({"id": 2, "cmd": "eval", "cards": ids}) + "\n" + '{"cmd":"quit"}\n')[0]
ev = [json.loads(l) for l in o.splitlines() if json.loads(l).get("id") == 2][0]["eval"]
WORDS = ("negate", "destroy", "banish", "return", "shuffle", "take control", "send", "becomes", "tribute")
groups = defaultdict(list)
for k, e in ev.items():
    if max(e.get("field", 0), e.get("set", 0), e.get("hand", 0), e.get("gy", 0)) < 1.5: continue
    desc = db.execute("select desc from texts where id=?", (int(k),)).fetchone()[0].lower().replace("graveyard", "gy")
    for sent in re.split(r"(?<=\.) ", desc.replace("\r", " ").replace("\n", " ")):
        if ":" not in sent: continue
        cond, act = sent.split(":", 1)
        if not any(w in act for w in WORDS): continue
        cond = re.sub(r"\(quick effect\)|\(this is a quick effect\)", "", cond).strip(" ,")
        if not cond or cond in ("once per turn", "once per chain"): continue
        for lead in ("once per turn, ", "once per chain, ", "during either player's turn, ", "during your opponent's turn, ", "during the main phase, "):
            if cond.startswith(lead): cond = cond[len(lead):]
        key = " ".join(re.sub(r'"[^"]*"', '"X"', cond).split()[:4])
        groups[key].append((e["name"], cond[:110], bool(e.get("needs"))))
        break
for key, rows in sorted(groups.items(), key=lambda kv: -len(kv[1]))[:60]:
    have = sum(1 for r in rows if r[2])
    print("%4d  (%d checked)  %s" % (len(rows), have, key))
    for r in rows[:nex]: print("        %-36s %s" % (r[0][:36], r[1]))
