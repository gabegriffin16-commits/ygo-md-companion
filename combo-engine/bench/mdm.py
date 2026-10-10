"""Import reference material from Master Duel Meta: decklists and guide combos (starting hand + end board).
Usage:
  python3 mdm.py guides <deck type>                          list guide spreadsheets for a deck type
  python3 mdm.py deck <deck type> <cards.cdb> <out.json>      latest top decklist -> {"main":[codes],"extra":[codes]}
  python3 mdm.py refs <spreadsheet id> <cards.cdb> <deck.json> <out.json>
                                                              guide combos -> a refs file for score.py / bench.py
A refs board: monsters in the end board go to the field, Spells/Traps to the backrow, "(in hand)" cards to the hand,
"GY" cards to the GY. Where the guide offers a choice ("Choice of 2 Traps"), the first ones are taken.
Starting hands keep generic slots ("Any Monster") as text: pick a real card for them before running bench.py."""
import json, sqlite3, sys, urllib.parse, urllib.request
API = "https://www.masterduelmeta.com/api/v1/"
def get(path, **q):
    url = API + path + ("?" + urllib.parse.urlencode(q) if q else "")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (mdc combo-engine bench)"})
    r = json.load(urllib.request.urlopen(req, timeout=30))
    return r if isinstance(r, list) else [r]   # a single match comes back as an object
def deck_type(name):
    r = get("deck-types", name=name)
    if not r: sys.exit("no deck type named %r" % name)
    return r[0]["_id"]
def cdb_maps(path):
    db = sqlite3.connect(path); by = {}; types = {}
    # Plain entries first; cards that are "always treated as" another card only have an aliased entry.
    for code, name, typ in db.execute("select d.id, t.name, d.type from datas d join texts t on t.id = d.id order by (d.alias != 0), d.id"):
        by.setdefault(name.lower(), code); types[code] = typ
    return by, types
def code_of(by, name):
    c = by.get(name.lower())
    if c is None: print("  ! not in cards.cdb: %s" % name, file=sys.stderr)
    return c

cmd = sys.argv[1] if len(sys.argv) > 1 else ""
if cmd == "guides":
    for s in get("spreadsheets", deckType=deck_type(sys.argv[2])):
        n = sum(len(sec.get("combos", [])) for t in s.get("tabs", []) for sec in t.get("sections", []))
        print(s["_id"], "|", (s.get("author") or {}).get("username"), "|", n, "combos |", (s.get("introduction") or "")[:100].replace("\n", " "))
elif cmd == "deck":
    name, cdb, out = sys.argv[2:5]
    by, _ = cdb_maps(cdb)
    t = get("top-decks", deckType=deck_type(name), limit=1, sort="-created")[0]
    rows = lambda part: [c for r in t.get(part, []) for c in [code_of(by, r["card"]["name"])] * r.get("amount", 1) if c]
    d = {"main": rows("main"), "extra": rows("extra"), "source": "https://www.masterduelmeta.com/top-decks" + t["url"], "date": t["created"][:10]}
    json.dump(d, open(out, "w"), indent=1)
    print("wrote %s: %d main, %d extra from %s" % (out, len(d["main"]), len(d["extra"]), d["source"]))
elif cmd == "refs":
    sid, cdb, deckf, out = sys.argv[2:6]
    by, types = cdb_maps(cdb)
    deck = json.load(open(deckf))
    s = get("spreadsheets", _id=sid)[0]
    boards = []; seen = set()
    def start_of(cb):   # "A or B": keep the first option only
        out, alt = [], False
        for x in cb.get("startingCards", []):
            if not alt: out.append(x["card"]["name"] if x.get("card") else "<%s>" % x.get("generic", "any card"))
            alt = x.get("connector") == "or"
        return out
    for tab in s.get("tabs", []):
        for sec in tab.get("sections", []):
            for c in sec.get("combos", []):
                if c["combo"] in seen: continue   # the same combo listed in several sections
                seen.add(c["combo"])
                cb = get("combos", _id=c["combo"])[0]
                b = {"name": cb["title"], "source": "https://www.masterduelmeta.com/combo/" + cb["_id"], "notes": cb.get("notes", ""),
                     "start": start_of(cb), "field": [], "backrow": [], "hand": [], "gy": []}
                taken = {}
                for x in cb.get("endBoard", []):
                    if not x.get("card"): continue
                    nm, lbl = x["card"]["name"], (x.get("label") or "").lower()
                    code = code_of(by, nm)
                    if not code: continue
                    if lbl.startswith("choice of"):   # "Choice of 2 Traps": take the first N offered
                        n = int(lbl.split()[2]) if lbl.split()[2].isdigit() else 1
                        if taken.get(lbl, 0) >= n: continue
                        taken[lbl] = taken.get(lbl, 0) + 1
                    where = "hand" if "hand" in lbl else "gy" if "gy" in lbl or "grave" in lbl else "field" if types.get(code, 0) & 1 else "backrow"
                    b[where].append(nm)
                boards.append(b)
                print("  %s: start %s -> field %s | backrow %s | hand %s" % (b["name"], b["start"], b["field"], b["backrow"], b["hand"]))
    ref = {"deck": deckf, "extra": deck["extra"], "sources": ["https://www.masterduelmeta.com/spreadsheet/" + sid], "author": (s.get("author") or {}).get("username"),
           "expect": "each guide board should rank at or near the top of what the engine finds from its starting hand", "boards": boards}
    json.dump(ref, open(out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("wrote %s: %d guide boards" % (out, len(boards)))
else:
    print(__doc__)
