"""Tally compare.py outputs honestly: per deck, how many comparable guide hands the engine matched (found the guide's
field), beat or tied by its own score, or missed. Not comparable: guide boards that use cards the decklist doesn't run,
and lines that aren't normal end boards ("vs <handtrap>", "under Droll", OTK / going second, Mulcharmy). Boards with
guessed placements are counted but flagged.
  python3 guidetally.py <compare output>...   (prints a table, then the missed hands)"""
import re, sys
sys.stdout.reconfigure(encoding="utf-8")
SPECIAL = re.compile(r"\b(vs\.?|versus|under|through|otk|ftk|going second|droll|ash|imperm|veiler|fuwalos|purulia|mulcharmy|nibiru|maxx)\b", re.I)
rows, missed = [], []
for f in sys.argv[1:]:
    name = re.sub(r".*[\\/]|\.txt$", "", f)
    c = {"hands": 0, "found": 0, "better": 0, "even": 0, "missed": 0, "missingCards": 0, "special": 0, "guessed": 0}
    cur = None
    for l in open(f, encoding="utf-8", errors="replace"):
        m = re.match(r"## (.*?)  \(start", l)
        if m: cur = m.group(1); continue
        m = re.match(r"\s+-> (.*)", l)
        if not m or cur is None: continue
        v = m.group(1); c["hands"] += 1
        if "cards not in this deck" in v: c["missingCards"] += 1; continue
        if SPECIAL.search(cur): c["special"] += 1; continue
        if "placement guessed" in v: c["guessed"] += 1
        if v.startswith("engine found"): c["found"] += 1
        elif v.startswith("engine better"): c["better"] += 1
        elif v.startswith("about even"): c["even"] += 1
        elif v.startswith("guide better"): c["missed"] += 1; missed.append((name, cur, v))
    rows.append((name, c))
print("%-18s %5s | %5s %6s %5s %6s | %s" % ("deck", "hands", "found", "better", "even", "missed", "not comparable (missing cards / handtrap-OTK lines), guessed"))
tot = {}
for name, c in rows:
    comp = c["found"] + c["better"] + c["even"] + c["missed"]
    print("%-18s %5d | %5d %6d %5d %6d | %d / %d not comparable of %d; %d with guessed placements" % (name, comp, c["found"], c["better"], c["even"], c["missed"], c["missingCards"], c["special"], c["hands"], c["guessed"]))
    for k, v in c.items(): tot[k] = tot.get(k, 0) + v
comp = tot["found"] + tot["better"] + tot["even"] + tot["missed"]
print("%-18s %5d | %5d %6d %5d %6d | %d / %d not comparable of %d" % ("TOTAL", comp, tot["found"], tot["better"], tot["even"], tot["missed"], tot["missingCards"], tot["special"], tot["hands"]))
if missed:
    print("\nMissed (guide board scores higher, comparable):")
    for name, h, v in missed: print("  %-16s %s  %s" % (name, h[:60], "[guessed]" if "guessed" in v else ""))
