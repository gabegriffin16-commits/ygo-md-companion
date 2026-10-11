"""Card scripts vs the text model, read from the rules engine itself: the engine loads each card in a duel and reports
every effect its script registered ({"cmd":"fx"}: effect type, event, categories, where it works from, once per turn),
helper-built effects included (Fusion.CreateSummonEff etc., which a static read of the Lua misses). Lists where that
disagrees with what the text model ("eval") credits the card with, per place (field / set / hand / GY).
  python3 fxdiag.py <engine> <cards.cdb> <scripts.zip> <deck.json>... [-v]
  -v: also print every card's opponent's-turn script effects.
The engine applies the text-only check itself (fx_check in main.cpp drops those values; "eval" lists them as noScript).
Disagreements:
  text-only    the text model credits an interruption from a place where the script has no opponent's-turn effect that
               can act at all (likely a text misread)
  script-only  the script has an opponent's-turn negate from a place the text model gives < 0.5 (categories don't say whose
               cards a removal hits, so only negates are listed)
"""
import json, sqlite3, subprocess, sys
args = [a for a in sys.argv[1:] if a != "-v"]; verbose = "-v" in sys.argv
exe, cdb, zipf = args[:3]; decks = args[3:]
db = sqlite3.connect(cdb)
def name(c): r = db.execute("select name from texts where id=?", (c,)).fetchone(); return r[0] if r else str(c)
EVENTS = {1002: "free", 1027: "chaining", 1028: "targeted", 1029: "destroyed", 1010: "destroy", 1014: "to GY", 1015: "leaves field", 1011: "banished",
          1100: "normal summoned", 1102: "special summoned", 1103: "normal summon", 1105: "special summon", 1104: "flip summon", 1108: "as material",
          1130: "attack", 1131: "attack target", 1012: "to hand", 1018: "discarded", 1017: "tributed"}
REMOVAL = {"negate": "negate", "disable": "negate", "disable_summon": "negate summon", "remove": "banish", "control": "control", "destroy": "destroy",
           "tohand": "bounce", "todeck": "spin", "tograve": "send", "pos": "position", "handes": "discard"}
NEG = {"negate", "negate summon"}
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
def ask(o):
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("ready") or r.get("error") or "fx" in r or "eval" in r): return r
ask({"id": 1, "cmd": "init", "cdb": cdb, "scripts": zipf})
cards = set()
for f in decks: d = json.load(open(f)); cards |= set(d["main"]) | set(d["extra"])
cards = sorted(cards)
ev = ask({"id": 2, "cmd": "eval", "cards": cards})["eval"]
fx = ask({"id": 3, "cmd": "fx", "cards": cards})["fx"]
def places(c, e):
    """Where an opponent's-turn effect works from: field / set / hand / gy (or "summon": a trigger on being summoned)."""
    typ = db.execute("select type from datas where id=?", (c,)).fetchone()[0]
    t, rng, code = set(e["type"]), e["range"], e["code"]
    out = set()
    if "activate" in t:
        if typ & 0x4 or typ & 0x10000: out.add("set")          # Trap / Quick-Play Spell activation
        return out
    if not (t & {"quick_o", "quick_f", "trigger_o", "trigger_f"}): return out
    if t & {"trigger_o", "trigger_f"} and "single" in t:
        if code in (1100, 1102): out.add("summon")              # "if this card is Summoned": counts when it lands on their turn
        elif code in (1029, 1014, 1015, 1011, 1028, 1010): out.add("gy")   # destroyed / sent / leaves / banished / targeted
        return out
    if t & {"trigger_o", "trigger_f"} and code in (1100, 1102, 1103, 1105) and "field" in t: pass   # "when your opponent Summons": ok below
    elif t & {"trigger_o", "trigger_f"} and code & 0x1000: return out   # phase triggers (End Phase / Standby): not an interruption
    if rng & (0x4 | 0x400 | 0x800): out.add("field")          # Monster Zone (Main / Extra Monster Zone variants)
    if rng & 0x8: out.update({"field", "set"} if typ & 0x4 else {"field"})   # face-up Continuous Traps act from the field too
    if rng & 0x2: out.add("hand")
    if rng & 0x10: out.add("gy")
    return out
textOnly, scriptOnly = [], []
for c in cards:
    e = ev.get(str(c)); effs = fx.get(str(c), [])
    if not e: continue
    for q in e.get("noScript", []): textOnly.append((name(c), q, 0.0))   # already dropped by the engine's own script check
    by = {}   # place -> kinds of interruption the script can do from there on their turn
    lines = []
    for x in effs:
        pl = places(c, x)
        if not pl: continue
        kinds = {"any"} | {REMOVAL[k] for k in x["cat"] if k in REMOVAL} | ({"summon"} if "spsummon" in x["cat"] or "fusion_summon" in x["cat"] else set())
        for q in pl: by.setdefault(q, set()).update(kinds)
        lines.append("%s %s on %s from %s: %s%s" % ("/".join(t for t in x["type"] if t not in ("field", "single", "actions")), x.get("desc", ""),
                     EVENTS.get(x["code"], x["code"]), ",".join(sorted(pl)), ",".join(x["cat"]) or "-", " (1/turn)" if x.get("count") else ""))
    if verbose and lines: print("%s\n  %s" % (name(c), "\n  ".join(lines)))
    text = {"field": max(e.get("field", 0), e.get("onSummon", 0) * 0), "set": e.get("set", 0), "hand": e.get("hand", 0), "gy": e.get("gy", 0)}
    for q, tv in text.items():
        have = by.get(q, set()) | (by.get("summon", set()) if q == "field" else set())
        if tv >= 1.0 and not have: textOnly.append((name(c), q, tv))
        if tv < 0.5 and have & NEG: scriptOnly.append((name(c), q, ",".join(sorted(have - {"any"}))))
p.stdin.write(json.dumps({"cmd": "quit"}) + "\n"); p.stdin.flush()
print("TEXT-ONLY (text credits an interruption from there; the script has no opponent's-turn effect from there):")
for n_, q, v in sorted(textOnly, key=lambda x: -x[2]): print("  %-44s %-5s %s" % (n_[:44], q, "text %.2f" % v if v else "(dropped by the engine's script check)"))
print("\nSCRIPT-ONLY (the script negates from there on their turn; the text model gives < 0.5):")
for n_, q, k in scriptOnly: print("  %-44s %-5s %s" % (n_[:44], q, k))
