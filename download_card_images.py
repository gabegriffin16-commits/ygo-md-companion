"""
Downloads the card images for the Omni HERO Duel Companion into an "images"
folder next to this script, named <card id>.jpg to match index.html.

Run it once (double-click download_card_images.bat), then upload the
"images" folder to your GitHub repo next to index.html.
Already-downloaded images are skipped, so it's safe to run again.
"""
import json, os, time, urllib.parse, urllib.request

API = "https://db.ygoprodeck.com/api/v7/cardinfo.php"
HEADERS = {"User-Agent": "OmniHERO-companion/1.0 (personal use)"}
CARD_IDS = {
    "Mulcharmy Fuwalos": 42141493,
    "Infinite Impermanence": 10045474,
    "A Hero Lives": 8949584,
    "Ash Blossom & Joyous Spring": 14558127,
    "Destiny HERO - Dangerous": 30757127,
    "Destiny HERO - Destroyer Phoenix Enforcer": 60461804,
    "Destiny HERO - Dystopia": 90579153,
    "Destiny HERO - Malicious": 9411399,
    "E - Emergency Call": 213326,
    "Elemental HERO Flame Wingman": 35809262,
    "Elemental HERO Flame Wingman - Infernal Rage": 93347961,
    "Elemental HERO Neos": 89943723,
    "Elemental HERO Shadow Mist": 50720316,
    "Elemental HERO Shining Neos Wingman": 56733747,
    "Elemental HERO Stratos": 40044918,
    "Elemental HERO Sunrise": 22908820,
    "Favorite Contact": 75047173,
    "Fusion Destiny": 52947044,
    "Instant Contact": 16169772,
    "Maxx \"C\"": 23434538,
    "Miracle Fusion": 45906428,
    "Polymerization": 24094653,
    "Vision HERO Faris": 18094166,
    "Vision HERO Increase": 22865492,
    "Vision HERO Trinity": 46759931,
    "Vision HERO Vyon": 27780618,
    "Xtra HERO Cross Crusader": 58004362,
    "Xtra HERO Infernal Devicer": 19324993,
    "Xtra HERO Wonder Driver": 1948619
}

def get(url):
    req = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()

def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images")
    os.makedirs(out, exist_ok=True)
    ids = ",".join(str(i) for i in CARD_IDS.values())
    data = json.loads(get(API + "?id=" + urllib.parse.quote(ids)))["data"]
    done = skipped = 0
    for card in data:
        path = os.path.join(out, f"{card['id']}.jpg")
        if os.path.exists(path):
            skipped += 1
            continue
        img = get(card["card_images"][0]["image_url"])
        with open(path, "wb") as f:
            f.write(img)
        done += 1
        print("Saved", card["name"])
        time.sleep(0.2)  # stay well under YGOPRODeck's rate limit
    print(f"\nDone. {done} downloaded, {skipped} already there.")
    print("Folder:", out)

if __name__ == "__main__":
    main()
