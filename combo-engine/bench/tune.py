"""Search tuning on a fixed work budget (reproducible: maxReplays instead of the clock). Runs a hand set across the
reference decks, beam-only, for each configuration and prints the total best score.
  python3 tune.py <engine> <cards.cdb> <scripts.zip> <replays per hand> <config.json>...
A config is a JSON object of search options, e.g. {} (defaults), {"width": 16}, {"growth": 2},
{"w": [W_MOVES, W_HAND, W_NS, W_GY]} (defaults [0.35, 0.25, 0.5, 0.1]). Env THREADS (default 8)."""
import json, os, sqlite3, subprocess, sys
exe, cdb, scripts, budget = sys.argv[1:5]; configs = [json.loads(a) for a in sys.argv[5:]] or [{}]
here = os.path.dirname(os.path.abspath(__file__))
db = sqlite3.connect(cdb)
names = {}
for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id = d.id order by (d.alias != 0), d.id"): names.setdefault(nm.lower(), c)
C = lambda *xs: [names[x.lower()] for x in xs]
HANDS = [("omni.json", [40044918, 18094166]), ("omni.json", [18094166, 9411399]), ("omni.json", [213326, 18094166]), ("omni.json", [27780618, 9411399]),
         ("elfnote.json", [13597785]),
         ("dracotail.json", C("Fallen of the White Dragon", "Dracotail Mululu", "Nibiru, the Primal Being")), ("dracotail.json", C("Branded Fusion")),
         ("dracotail.json", C("Dracotail Lukias", "Dracotail Faimena")),
         ("branded.json", C("Fallen of the White Dragon", "Blazing Cartesia, the Virtuous", "Nibiru, the Primal Being")),
         ("kewltune.json", C("Kewl Tune Rotary", "Kewl Tune Reco")), ("kewltune.json", C("Kewl Tune Cue"))]
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready")): return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": scripts})
decks = {}
qid = 10
for cfg in configs:
    tot, row = 0.0, []
    for dk, hand in HANDS:
        d = decks.setdefault(dk, json.load(open(os.path.join(here, dk))))
        main = list(d["main"])
        for c in hand:
            if c in main: main.remove(c)
        qid += 1
        q = {"id": qid, "cmd": "search", "deck": main, "extra": d["extra"], "hand": hand, "timeMs": 600000, "maxReplays": int(budget),
             "threads": int(os.environ.get("THREADS") or 8), "top": 1, "maxActions": 16, "mode": "beam"}
        q.update(cfg)
        r = ask(q)
        s = r["boards"][0]["score"] if r.get("boards") else 0
        tot += s; row.append("%.2f" % s)
    print("%-40s total %7.2f  | %s" % (json.dumps(cfg), tot, " ".join(row)), flush=True)
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
