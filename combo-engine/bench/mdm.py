"""Import reference material from Master Duel Meta: decklists and guide combos (starting hand + end board).
Usage:
  python3 mdm.py guides <deck type>                          list guide spreadsheets for a deck type
  python3 mdm.py deck <deck type> <cards.cdb> <out.json>      latest top decklist -> {"main":[codes],"extra":[codes]}
  python3 mdm.py refs <spreadsheet id> <cards.cdb> <deck.json> <out.json>
                                                              guide combos -> a refs file for score.py / bench.py
A refs board: each end-board card goes where the guide's steps last put it (summoned -> field, Set Spell/Trap ->
backrow, used as material / sent -> GY, searched or "moved" -> hand, banished); a label ("(in hand)", "GY") wins over
that. Cards the steps don't place are guessed (monsters -> field, Spells/Traps -> backrow) and listed under "guessed",
except ones shown as what another card makes ("Ecclesia = Fallen of Albaz"): those aren't on the board. "A or B" and
"Choice of 2 Traps" take the first ones. Cards the guide uses that the decklist lacks are listed under "missing"
(compare.py flags those boards). Starting hands keep generic slots ("Any Monster") as text.
Env MDM_CACHE=<folder> keeps fetched combos for re-runs."""
import json, os, re, sqlite3, sys, urllib.parse, urllib.request
API = "https://www.masterduelmeta.com/api/v1/"
def get(path, **q):
    url = API + path + ("?" + urllib.parse.urlencode(q) if q else "")
    cache = os.environ.get("MDM_CACHE")   # a folder: combos are kept as <id>.json there and reused
    cf = os.path.join(cache, q["_id"] + ".json") if cache and path == "combos" and "_id" in q else None
    if cf and os.path.exists(cf): r = json.load(open(cf, encoding="utf-8"))
    else:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (mdc combo-engine bench)"})
        r = json.load(urllib.request.urlopen(req, timeout=60))
        if cf: json.dump(r, open(cf, "w", encoding="utf-8"))
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
def where_steps(cb, types, by, ends):
    """Where each named card is after the guide's steps. An entry's connector links it to the next entry, so in
    "A + B = summon C" the entries before "=" are materials (-> GY) and the one after is what's summoned."""
    at = {}
    for s in cb.get("steps", []):
        es = s.get("entries", [])
        cons = [e.get("connector") for e in es]
        made = [(es[i].get("card") or {}).get("name") for i in range(1, len(es)) if cons[i - 1] == "equals" or (cons[i - 1] == "arrow" and i > 1 and cons[i - 2] == "plus")]
        optional = any(m and m not in ends for m in made)   # it made something that isn't on the end board: an optional push
        for i, e in enumerate(es):
            nm = (e.get("card") or {}).get("name")
            if not nm: continue
            sym, con = e.get("symbol"), e.get("connector")
            prev = es[i - 1].get("connector") if i else None
            t = types.get(by.get(nm.lower()), 1)
            spell = not (t & 1); lasting = t & (0x20000 | 0x40000 | 0x80000)   # Continuous / Equip / Field stay out
            if sym in ("summon",): at[nm] = "field"
            elif sym == "set_st" or (sym == "set" and spell): at[nm] = "backrow"
            elif sym == "set": at[nm] = "field"
            elif sym in ("search", "hand"): at[nm] = "hand"
            elif sym == "graveyard": at[nm] = "gy"
            elif sym == "banish": at[nm] = "banished"
            elif sym == "move": at[nm] = "hand"   # in these guides "move" is a card returning to the hand
            elif sym is None and (con in ("plus", "equals") or prev == "plus"):
                if not optional: at[nm] = "gy"   # material
            elif sym is None and (prev == "equals" or (prev == "arrow" and i > 1 and es[i - 2].get("connector") == "plus")): at[nm] = "field"   # what they made
            elif sym is None and not con and not prev and len(es) == 1: at[nm] = "field"   # a card shown on its own: back on the field
            elif sym in ("effect", "chain") and spell and at.get(nm) != "backrow": at[nm] = "backrow" if lasting else "gy"   # a Spell activated from hand
    return at
def named_cards(cb):
    out = []
    for x in cb.get("startingCards", []) + cb.get("endBoard", []) + cb.get("requiredCards", []) + [e for s in cb.get("steps", []) for e in s.get("entries", [])]:
        c = x.get("card") or (x if "description" in x else None)
        if c and c.get("name"): out.append(c["name"])
    return out

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
                taken = {}; steps = where_steps(cb, types, by, {(x.get("card") or {}).get("name") for x in cb.get("endBoard", [])}); b["guessed"] = []
                alt = False; after = None
                for x in cb.get("endBoard", []):
                    skip, alt = alt, x.get("connector") == "or"   # "A or B": keep A
                    made, after = after in ("equals", "arrow"), x.get("connector")   # "Ecclesia = Fallen of Albaz": what it makes on their turn
                    if skip or not x.get("card"): continue
                    if made and x["card"]["name"] not in steps: continue
                    nm, lbl = x["card"]["name"], (x.get("label") or "").lower()
                    code = code_of(by, nm)
                    if not code: continue
                    if lbl.startswith("choice of"):   # "Choice of 2 Traps": take the first N offered
                        n = int(lbl.split()[2]) if lbl.split()[2].isdigit() else 1
                        if taken.get(lbl, 0) >= n: continue
                        taken[lbl] = taken.get(lbl, 0) + 1
                    where = "hand" if re.search(r"hand", lbl) else "gy" if re.search(r"gy|grave", lbl) else steps.get(nm)
                    if where is None:
                        where = "field" if types.get(code, 0) & 1 else "backrow"; b["guessed"].append(nm)
                    b.setdefault(where, []).append(nm)
                inDeck = set(deck["main"]) | set(deck["extra"])
                b["missing"] = sorted({nm for nm in named_cards(cb) if by.get(nm.lower()) and by[nm.lower()] not in inDeck and not types.get(by[nm.lower()], 0) & 0x4000})
                for k in ("guessed", "missing", "banished"):
                    if not b.get(k): b.pop(k, None)
                boards.append(b)
                print("  %s: start %s -> field %s | backrow %s | hand %s | gy %s%s%s" % (b["name"], b["start"], b["field"], b["backrow"], b["hand"], b["gy"],
                      " | guessed %s" % b["guessed"] if b.get("guessed") else "", " | MISSING %s" % b["missing"] if b.get("missing") else ""))
    ref = {"deck": deckf, "extra": deck["extra"], "sources": ["https://www.masterduelmeta.com/spreadsheet/" + sid], "author": (s.get("author") or {}).get("username"),
           "expect": "each guide board should rank at or near the top of what the engine finds from its starting hand", "boards": boards}
    json.dump(ref, open(out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("wrote %s: %d guide boards" % (out, len(boards)))
else:
    print(__doc__)
