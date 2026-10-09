"""
Refreshes cards.json (the full card database the companion uses for card lookup,
legal targets and card facts) from the YGOPRODeck API. A GitHub Action runs this
every week, so you normally don't need to. Running it by hand still works.
"""
import json, os, re, urllib.request, urllib.parse
from datetime import date

API = "https://db.ygoprodeck.com/api/v7/cardinfo.php?misc=yes"
RARITY = {"Ultra Rare": "UR", "Super Rare": "SR", "Rare": "R", "Normal": "N", "Common": "N"}
FIELDS = [("race", "r"), ("attribute", "a"), ("atk", "atk"), ("def", "def"), ("level", "lv"),
          ("linkval", "lk"), ("linkmarkers", "lm"), ("archetype", "ar")]

# Master Duel's own Forbidden/Limited list and card popularity. YGOPRODeck only tracks the TCG/OCG lists,
# so these come from Master Duel Meta's card API, which follows each Master Duel list update.
MDM_API = "https://www.masterduelmeta.com/api/v1/cards?limit=3000&page="
BAN_CODES = {"Forbidden": "F", "Limited 1": "L", "Limited 2": "S"}

def key(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())

def fetch_mdm():
    """Returns (ban, pop): {normalized name: "F"/"L"/"S"} and {normalized name: popularity rank},
    or None if Master Duel Meta couldn't be reached (the previous values are kept then)."""
    rows = []
    try:
        for page in range(1, 20):
            req = urllib.request.Request(MDM_API + str(page), headers={"User-Agent": "MasterDuelCompanion/1.0 (personal use)"})
            with urllib.request.urlopen(req, timeout=120) as r:
                part = json.load(r)
            if not isinstance(part, list):
                return None
            rows += part
            if len(part) < 3000:
                break
    except Exception as e:
        print("Master Duel Meta download failed, keeping the previous banlist/popularity:", e)
        return None
    ban, pop = {}, {}
    for row in rows:
        k = key(row.get("name", ""))
        code = BAN_CODES.get(row.get("banStatus") or "")
        if code:
            ban[k] = code
        rank = row.get("popRank")
        if isinstance(rank, (int, float)) and rank < 1e9:
            pop[k] = min(pop.get(k, rank), int(rank))
    if len(rows) < 5000 or sum(1 for v in ban.values() if v == "F") < 20:
        return None        # something's off: keep the last good data instead
    return ban, pop

def main():
    req = urllib.request.Request(API, headers={"User-Agent": "OmniHERO-companion/1.0 (personal use)"})
    print("Downloading card database (about 25 MB)...")
    with urllib.request.urlopen(req, timeout=120) as r:
        data = json.load(r)["data"]
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "cards.json")
    mdm = fetch_mdm()
    if mdm is None:                    # keep last week's values rather than wiping them
        try:
            with open(out, encoding="utf-8") as f:
                old = json.load(f)["cards"]
            mdm = ({key(c["n"]): c["b"] for c in old if c.get("b")}, {key(c["n"]): c["p"] for c in old if c.get("p")})
        except (OSError, ValueError, KeyError):
            mdm = ({}, {})
    ban, pop = mdm
    cards = []
    for c in data:
        misc = (c.get("misc_info") or [{}])[0]
        o = {"id": c["id"], "n": c["name"], "t": c.get("type"), "f": c.get("frameType"),
             "d": c.get("desc", "").replace("\r\n", "\n")}
        if c.get("typeline"): o["tl"] = c["typeline"]
        for k, s in FIELDS:
            if c.get(k) is not None: o[s] = c[k]
        if misc.get("md_rarity"): o["md"] = RARITY.get(misc["md_rarity"], misc["md_rarity"])
        if ban.get(key(c["name"])): o["b"] = ban[key(c["name"])]
        if pop.get(key(c["name"])): o["p"] = pop[key(c["name"])]
        cards.append(o)
    print(f"Master Duel banlist: {sum(1 for c in cards if c.get('b') == 'F')} Forbidden, "
          f"{sum(1 for c in cards if c.get('b') == 'L')} Limited, {sum(1 for c in cards if c.get('b') == 'S')} Semi-Limited")
    # Only rewrite the file when the card data actually changed, so the weekly job stays quiet.
    try:
        with open(out, encoding="utf-8") as f:
            if json.load(f).get("cards") == cards:
                print(f"No changes ({len(cards)} cards).")
                check_deck(here, cards)
                return
    except (OSError, ValueError):
        pass
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"updated": date.today().isoformat(), "source": "YGOPRODeck API v7",
                   "banlist": "Master Duel Forbidden/Limited list and popularity (p) via Master Duel Meta", "cards": cards},
                  f, ensure_ascii=False, separators=(",", ":"))
    print(f"Saved {len(cards)} cards to {out}")
    check_deck(here, cards)

def check_deck(here, cards):
    """Warn about deck.json names that aren't in the database (usually a typo)."""
    try:
        with open(os.path.join(here, "deck.json"), encoding="utf-8") as f:
            deck = json.load(f)
    except (OSError, ValueError):
        return
    names = {c["n"] for c in cards}
    missing = [c["name"] for c in deck.get("main", []) + deck.get("extra", []) if c["name"] not in names]
    for n in missing:
        print(f"WARNING: deck.json card not found in the database: {n}")

if __name__ == "__main__":
    main()
