"""A guide's starting hand as card codes: named cards as they are, typed slots ("<Lv3 Tuner>", "<Dtail name>",
"<Spellcaster>") become the deck card that fits (most copies, not a handtrap), generic slots ("<Any card>") the filler.
Same rules as compare.py.  hand_of(db, names, start, main, filler) -> [codes] or None"""
import re
RACES = ["warrior", "spellcaster", "fairy", "fiend", "zombie", "machine", "aqua", "pyro", "rock", "winged beast", "plant", "insect", "thunder",
         "dragon", "beast", "beast-warrior", "dinosaur", "fish", "sea serpent", "reptile", "psychic", "divine-beast", "creator god", "wyrm", "cyberse", "illusion"]
def subseq(a, b): it = iter(b); return all(ch in it for ch in a)
def race_of(slot):
    w = slot.replace(" monster", "").strip()
    for i, r in enumerate(RACES):
        if r == w: return 1 << i
    for i, r in enumerate(RACES):
        if len(w) >= 4 and r.startswith(w): return 1 << i
    return 0
def fits(db, slot, c):
    r = db.execute("select t.name, d.level, d.type, d.race from datas d join texts t on t.id = d.id where d.id=?", (c,)).fetchone()
    if not r: return False
    nm, lv, typ = r[0].lower(), r[1] & 0xff, r[2]
    if race_of(slot): return bool(typ & 1 and r[3] & race_of(slot))
    for w in re.findall(r"[a-z0-9]+", slot):
        if w in ("name", "card", "cards", "any"): continue
        if w in ("tuner", "tuners"):
            if not typ & 0x1000: return False
            continue
        if w in ("monster", "monsters"):
            if not typ & 1: return False
        elif w in ("spell", "trap"):
            if not typ & (2 if w == "spell" else 4): return False
        elif re.fullmatch(r"(lv|level)(\d+)", w):
            if not typ & 1 or lv != int(re.fullmatch(r"(lv|level)(\d+)", w).group(2)): return False
        elif not any(subseq(w, x) for x in re.findall(r"[a-z0-9]+", nm)): return False
    return True
def handtrap(db, c):
    d = (db.execute("select desc from texts where id=?", (c,)).fetchone() or [""])[0].lower()
    return "(quick effect)" in d and any(x in d for x in ("discard this card", "send this card from your hand", "from your hand to the gy", "this card in your hand"))
def pick(db, slot, main, hand):
    left = list(main)
    for c in hand:
        if c in left: left.remove(c)
    for alt in re.split(r"[/,]", slot):
        cands = [c for c in dict.fromkeys(left) if fits(db, alt.strip(), c)]
        if cands: return max(cands, key=lambda c: (not handtrap(db, c), left.count(c)))
    return None
def hand_of(db, names, start, main, filler):
    hand = []
    for s in start:
        if s.startswith("<"):
            slot = s.strip("<>").lower().split("(")[0].strip()
            if slot in ("any", "any card", "any monster", "discard"): hand.append(filler); continue
            c = pick(db, slot, main, hand)
            if c is None: return None
            hand.append(c); continue
        c = names.get(s.lower())
        if c is None: return None
        hand.append(c)
    return hand
