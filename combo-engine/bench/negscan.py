"""Whole-database scan for text-model negates the scripts don't back: cards the text scores >= 3.0 in a place (field / set /
hand / GY) whose opponent's-turn script effects there all have categories and none is a negate. Hits are candidates to read
by hand (revivers and take-control cards score >= 3.0 by design and show up too).
  python3 negscan.py <engine> <cards.cdb> <scripts.zip>   (writes nothing; prints the list)"""
import json, subprocess, sqlite3, sys
sys.stdout.reconfigure(encoding="utf-8")
exe, cdb, zipf = sys.argv[1:4]
db = sqlite3.connect(cdb)
ids = [r[0] for r in db.execute("select d.id from datas d join texts t on t.id=d.id where d.alias=0 and d.ot & 3 and d.type & 0x4000 = 0 and d.id < 100000000")]
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
o = p.communicate(json.dumps({"id":1,"cmd":"init","cdb":cdb,"scripts":zipf}) + "\n" + json.dumps({"id":2,"cmd":"eval","cards":ids}) + "\n" + json.dumps({"id":3,"cmd":"fx","cards":ids}) + "\n" + '{"cmd":"quit"}\n')[0]
R = {json.loads(l).get("id"): json.loads(l) for l in o.splitlines()}
ev, fx = R[2]["eval"], R[3]["fx"]
out = []
for k, e in ev.items():
    typ = db.execute("select type from datas where id=?", (int(k),)).fetchone()[0]
    trapLike = typ & 0x4 or typ & 0x10000
    for place, rbit in (("field", 0x4 | 0x400 | 0x800 | (0 if trapLike else 0x8)), ("set", 0x8), ("hand", 0x2), ("gy", 0x10)):
        if e.get(place, 0) < 3.0: continue
        effs = []
        for x in fx.get(k, []):
            t = set(x["type"])
            if not (t & {"quick_o", "quick_f", "trigger_o", "trigger_f", "activate"}): continue
            if "activate" in t: ok = place in ("set", "field") and trapLike
            elif "single" in t: ok = place in ("field", "gy", "set")
            else: ok = bool(x["range"] & rbit)
            if ok: effs.append(x)
        cats = set(c for x in effs for c in x["cat"])
        if effs and all(x["cat"] for x in effs) and not cats & {"negate", "disable", "disable_summon"}:
            out.append((e["name"], place, round(e[place], 2), sorted(cats)))
print(len(out))
for n, pl, v, c in out: print("%-42s %-5s %.2f  script: %s" % (n[:42], pl, v, ",".join(c)))
