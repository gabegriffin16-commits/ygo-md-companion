"""
Refreshes cards.json (the full card database the companion uses for card lookup,
legal targets and card facts) from the YGOPRODeck API. A GitHub Action runs this
every week, so you normally don't need to. Running it by hand still works.
"""
import json, os, urllib.request
from datetime import date

API = "https://db.ygoprodeck.com/api/v7/cardinfo.php?misc=yes"
RARITY = {"Ultra Rare": "UR", "Super Rare": "SR", "Rare": "R", "Normal": "N", "Common": "N"}
FIELDS = [("race", "r"), ("attribute", "a"), ("atk", "atk"), ("def", "def"), ("level", "lv"),
          ("linkval", "lk"), ("linkmarkers", "lm"), ("archetype", "ar")]

def main():
    req = urllib.request.Request(API, headers={"User-Agent": "OmniHERO-companion/1.0 (personal use)"})
    print("Downloading card database (about 25 MB)...")
    with urllib.request.urlopen(req, timeout=120) as r:
        data = json.load(r)["data"]
    cards = []
    for c in data:
        misc = (c.get("misc_info") or [{}])[0]
        o = {"id": c["id"], "n": c["name"], "t": c.get("type"), "f": c.get("frameType"),
             "d": c.get("desc", "").replace("\r\n", "\n")}
        if c.get("typeline"): o["tl"] = c["typeline"]
        for k, s in FIELDS:
            if c.get(k) is not None: o[s] = c[k]
        if misc.get("md_rarity"): o["md"] = RARITY.get(misc["md_rarity"], misc["md_rarity"])
        cards.append(o)
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "cards.json")
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
        json.dump({"updated": date.today().isoformat(), "source": "YGOPRODeck API v7", "cards": cards},
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
