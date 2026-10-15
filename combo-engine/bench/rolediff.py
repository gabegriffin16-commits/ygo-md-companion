"""Role stability across guide builds of one deck: each card's role (and starter kind) per run, flips marked, plus each
starter's main-line score per run. Different seeds deal different hands, so a stable guide gives every card the same role.
  python3 rolediff.py <cards.cdb> <starters out.json>...   (starters.py outputs)"""
import json, sqlite3, sys
sys.stdout.reconfigure(encoding="utf-8")
db = sqlite3.connect(sys.argv[1])
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0]
runs = [json.load(open(f)) for f in sys.argv[2:]]
roles = [{c["card"]: c["role"] + ((" " + c["kind"]) if c.get("kind") else "") for c in r["cards"]} for r in runs]
mains = [{s["card"]: s["main"]["score"] for s in r.get("starters", [])} for r in runs]
cards = sorted(set().union(*roles), key=nm)
flips = 0
print("%-36s %s" % ("card", " | ".join("run %d" % (i + 1) for i in range(len(runs)))))
for c in cards:
    rs = [r.get(c, "-") for r in roles]
    f = len(set(rs)) > 1; flips += f
    ms = ["%.1f" % m[c] if c in m else "" for m in mains]
    print("%s %-34s %s   %s" % ("*" if f else " ", nm(c)[:34], " | ".join("%-17s" % x for x in rs), " ".join(ms)))
print("\n%d of %d cards change role across %d runs" % (flips, len(cards), len(runs)))
for i, r in enumerate(runs): print("run %d: %d hands, good board >= %.2f, %d starters" % (i + 1, r["opening"]["hands"], r["opening"]["good"], len(r.get("starters", []))))
