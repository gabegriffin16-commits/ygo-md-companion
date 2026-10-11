"""Card scripts vs the text model: reads each card's Lua script (ProjectIgnis CardScripts) for the structure of its
effects (type: Quick Effect / trigger / ignition / activated Spell-Trap; the event; categories: negate, destroy,
banish...; where it works from; once per turn) and lists where that disagrees with what the engine's text model reads
from the card ("eval": its interruption value on the field / set / in hand / in the GY).
  python3 scriptdiag.py <engine> <cards.cdb> <scripts.zip> <deck.json>...
Disagreements:
  text-only    the text model counts an interruption, but the script has no effect usable on the opponent's turn that
               negates / removes / summons (likely a text misread)
  script-only  the script has an opponent's-turn effect that negates or removes, but the text model gives it nothing
"""
import json, re, sqlite3, subprocess, sys, zipfile
exe, cdb, zipf = sys.argv[1:4]; decks = sys.argv[4:]
db = sqlite3.connect(cdb)
def name(c): r = db.execute("select name from texts where id=?", (c,)).fetchone(); return r[0] if r else str(c)
z = zipfile.ZipFile(zipf); index = {}
for n in z.namelist():
    m = re.search(r"/c(\d+)\.lua$", n)
    if m: index.setdefault(int(m.group(1)), n)
DISRUPT = {"CATEGORY_NEGATE": "negate", "CATEGORY_DISABLE": "negate effect", "CATEGORY_DESTROY": "destroy", "CATEGORY_REMOVE": "banish",
           "CATEGORY_TOHAND": "to hand", "CATEGORY_TODECK": "to deck", "CATEGORY_CONTROL": "control", "CATEGORY_TOGRAVE": "send to GY",
           "CATEGORY_POSITION": "position", "CATEGORY_DISABLE_SUMMON": "negate summon"}
def effects(code):
    """Each Effect.CreateEffect block: its Set* calls."""
    n = index.get(code)
    if not n: return None
    src = z.read(n).decode("utf8", "replace")
    out = {}
    for m in re.finditer(r"local\s+(\w+)\s*=\s*Effect\.CreateEffect", src): out[m.group(1)] = {"var": m.group(1)}
    for var in list(out):
        e = out[var]
        for k in ("SetType", "SetCode", "SetCategory", "SetRange", "SetCountLimit", "SetProperty"):
            mm = re.search(r"\b%s:%s\(([^)\n]*)\)" % (re.escape(var), k), src)
            if mm: e[k] = mm.group(1)
        e["registered"] = bool(re.search(r"RegisterEffect\(\s*%s\b" % re.escape(var), src))
    return list(out.values())
def their_turn(e, typ):
    t = e.get("SetType", ""); code = e.get("SetCode", "")
    if "QUICK" in t: return True
    if "ACTIVATE" in t and (typ & 0x4 or typ & 0x10000): return True   # Trap / Quick-Play activation
    if "TRIGGER" in t and any(x in code for x in ("EVENT_SPSUMMON", "EVENT_SUMMON", "EVENT_CHAINING", "EVENT_ATTACK", "EVENT_BE_BATTLE")): return True
    return False
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"]: return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": zipf})
cards = set()
for f in decks: d = json.load(open(f)); cards |= set(d["main"]) | set(d["extra"])
ev = ask({"id": 2, "cmd": "eval", "cards": sorted(cards)})["eval"]
textOnly, scriptOnly = [], []
for c in sorted(cards):
    e = ev.get(str(c));
    if not e: continue
    typ = db.execute("select type from datas where id=?", (c,)).fetchone()[0]
    fx = effects(c)
    if fx is None: continue
    tv = max(e.get("field", 0), e.get("set", 0), e.get("hand", 0), e.get("gy", 0))
    disrupt = []
    for x in fx:
        if not x["registered"] or not their_turn(x, typ): continue
        cats = [DISRUPT[k] for k in DISRUPT if k in x.get("SetCategory", "")]
        if cats: disrupt.append("%s %s from %s" % ("/".join(cats), x.get("SetCode", "").replace("EVENT_", "on ").lower(), x.get("SetRange", "activation").replace("LOCATION_", "").lower()))
    summons = any(x["registered"] and their_turn(x, typ) and "CATEGORY_SPECIAL_SUMMON" in x.get("SetCategory", "") for x in fx)
    if tv >= 1.0 and not disrupt and not summons: textOnly.append((name(c), tv))
    if tv < 0.5 and disrupt: scriptOnly.append((name(c), "; ".join(disrupt)))
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
print("TEXT-ONLY (text counts an interruption; the script has no opponent's-turn negate / removal / summon):")
for n_, v in sorted(textOnly, key=lambda x: -x[1]): print("  %-44s text %.2f" % (n_[:44], v))
print("\nSCRIPT-ONLY (the script negates / removes on their turn; the text model gives it < 0.5):")
for n_, d_ in scriptOnly: print("  %-44s %s" % (n_[:44], d_[:110]))
