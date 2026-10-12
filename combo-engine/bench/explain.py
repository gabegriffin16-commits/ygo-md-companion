"""Step explanations for a line (Guide Crafter). Simple steps get one line; defining steps get the reasoning.
Built from the line's own data, no guessing: each step's picks (grouped by the game's hint: add / Special Summon / send /
material...) say what it produced and what it used, so later steps that use a card an earlier step produced depend on it.
End-board points (the score's breakdown) are credited to the step that put each card there, and every step gets the
points its chain leads to. Defining = a step that puts an interruption on the board, an Extra Deck summon that leads to a
real share of the board, or a hub several later steps need; capped at MAXKEY per line so the page stays readable.
  from explain import explain; explain(line, why, nm, ctype, kindof) -> ([{"n", "text", "key", "detail": [...], "points", "for"}], points the line built)
line = starters.py brief() (steps = [do, card, effect, picks, groups]); why = the engine's score breakdown for its board;
nm(code) -> name; ctype(code) -> cdb type bits."""
HINT = {500: "Tribute", 501: "discard", 502: "destroy", 503: "banish", 504: "send to the GY", 505: "return to the hand", 506: "add", 507: "shuffle into the Deck",
        508: "Normal Summon", 509: "Special Summon", 510: "Set", 514: "flip", 518: "equip", 519: "detach", 527: "place", 531: "Tribute", 532: "detach", 578: "attach", 579: "return to the GY"}
MAT = {511, 512, 513, 533}
MAKES = {506, 509, 510, 527, 504, 579}     # picks that put a card somewhere a later step can use it (hand, field, Spell/Trap Zone, GY)
SPENDS = MAT | {500, 501, 503, 531}        # picks the step gives up (materials, costs)
TYPE_FUSION, TYPE_SYNCHRO, TYPE_XYZ, TYPE_LINK = 0x40, 0x2000, 0x800000, 0x4000000
MAXKEY = 4

def lst(a):
    return a[0] if len(a) == 1 else ", ".join(a[:-1]) + " and " + a[-1] if a else ""

def step_text(st, nm):
    """One line, the app's wording (index.html cfStepText)."""
    do, c, fx, picks, groups = st
    n = nm(c)
    if do == "Normal Summon": return "Normal Summon " + n
    if do == "Opp": return "They activate " + n
    mats = [nm(x) for h, cs in (groups or []) if h in MAT for x in cs]
    if do == "Special Summon": return "Special Summon " + n + (" using " + lst(mats or [nm(x) for x in picks]) if (mats or picks) else "")
    out = []
    for h, cs in groups or []:
        names = [nm(x) for x in cs]
        if not names: continue
        if h in MAT:
            if out and out[-1].startswith("Special Summon ") and " using " not in out[-1]: out[-1] += " using " + lst(names)
            else: out.append("use " + lst(names) + " as material")
        else: out.append((HINT.get(h) or "choose") + " " + lst(names))
    fx = " ".join((fx or "").split()).rstrip(".")
    if out:
        t = ", ".join(out)
        if fx and len(fx) <= 40 and not t.lower().startswith(fx.lower()): return "%s: %s (%s)" % (n, fx, t)
        return "%s: %s" % (n, t[0].upper() + t[1:])
    return "%s: %s" % (n, fx or ("use its effect" if do == "Use" else "activate"))

def made_by(st):
    do, c, fx, picks, groups = st
    # Every pick ends up somewhere a later step can use it: added / summoned / Set, and also materials and costs (now in
    # the GY or banished, where their own effects trigger: a Dracotail sent as Fusion material Sets a Trap).
    m = set(x for h, cs in (groups or []) for x in cs)
    if do in ("Normal Summon", "Special Summon"): m.add(c)
    return m

def used_by(st):
    do, c, fx, picks, groups = st
    u = set(x for h, cs in (groups or []) if h in SPENDS for x in cs)
    if do != "Opp": u.add(c)
    return u

def card_points(line, why, nm, kindof=lambda c: ""):
    """Points per card on the end board (its interruptions + the small extras named after it) and what to call them:
    the interruption kind when it has a stop, "Lock" when its points are a lock, else nothing (just a body)."""
    byname = {}
    for c in line["field"] + line["backrow"] + line.get("gy", []) + line.get("banished", []) + line.get("left", []): byname.setdefault(nm(c), c)
    pts, lab = {}, {}
    for s in why.get("stops", []):
        c = s.get("code")
        if c: pts[c] = pts.get(c, 0) + s["counts"]; lab[c] = kindof(c) or "Stop"
    for x in why.get("flat", []):
        w = x["what"].split(": ", 1)
        c = byname.get(w[1]) if len(w) == 2 else None
        if c:
            pts[c] = pts.get(c, 0) + x["value"]
            if w[0] == "lock" and c not in lab: lab[c] = "Lock"
    return pts, lab

def explain(line, why, nm, ctype, kindof=lambda c: ""):
    steps = line["steps"]; n = len(steps)
    # Who produced each card most recently, and which steps each step's cards came from.
    last, deps, feeds = {}, [set() for _ in steps], [set() for _ in steps]
    for i, st in enumerate(steps):
        for c in used_by(st):
            if c in last and last[c] != i: deps[i].add(last[c]); feeds[last[c]].add(i)
        for c in made_by(st): last[c] = i
    pts, kind = card_points(line, why, nm, kindof)
    # Compare with what the line built: cards held from the opening hand (handtraps) aren't the line's doing.
    total = why.get("total", 0) - sum(pts.get(c, 0) for c in set(line.get("left", [])) if c not in last)
    total = total if total > 0 else 1
    # Credit: the step that last produced the card (cards never produced in the line came from the hand: no step).
    direct = [0.0] * n; direct_cards = [[] for _ in steps]
    for c, v in pts.items():
        if c in last: direct[last[c]] += v; direct_cards[last[c]].append(c)
    # What each step's chain leads to: its own points + those of every step that depends on it (each step once).
    reach = []
    for i in range(n):
        seen, todo = {i}, [i]
        while todo:
            j = todo.pop()
            for k in feeds[j]:
                if k not in seen: seen.add(k); todo.append(k)
        reach.append(sum(direct[j] for j in seen))
    ed = lambda c: bool(ctype(c) & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK))
    cand = []
    for i, st in enumerate(steps):
        if st[0] == "Opp": continue
        payoff = any(pts.get(c, 0) >= 1.5 and c in kind for c in direct_cards[i])
        summons_ed = st[0] == "Special Summon" and ed(st[1]) or any(ed(x) for h, cs in (st[4] or []) if h == 509 for x in cs)
        hub = len(feeds[i]) >= 3
        if payoff or (summons_ed and reach[i] >= 0.25 * total) or hub: cand.append((reach[i] + direct[i], i))
    key = set(i for _, i in sorted(cand, reverse=True)[:MAXKEY])
    out = []
    for i, st in enumerate(steps):
        e = {"n": i + 1, "text": step_text(st, nm), "key": i in key, "points": round(reach[i], 1)}
        if i in key:
            d = []
            fx = " ".join((st[2] or "").split())
            if len(fx) > 40: d.append("What it does: " + fx + ("" if fx.endswith(".") else "."))
            came = sorted((j, c) for j in deps[i] for c in used_by(st) if last_before(steps, c, i) == j)
            if came: d.append("Uses " + lst(["%s (from step %d)" % (nm(c), j + 1) for j, c in came]) + ".")
            if feeds[i]:
                nxt = sorted(feeds[i])
                d.append("Sets up " + "; ".join("step %d (%s)" % (k + 1, short(step_text(steps[k], nm))) for k in nxt[:3]) + ("; and more" if len(nxt) > 3 else "") + ".")
            if direct_cards[i]:
                d.append("On the end board: " + lst(["%s (%s%.1f points)" % (nm(c), (kind[c].lower() + ", ") if c in kind else "", pts[c]) for c in direct_cards[i]]) + ".")
            if reach[i] >= total - 0.05:
                d.append("Everything this line builds (%.1f points) depends on it." % total)
            elif reach[i] > direct[i] + 0.05:
                d.append("With what it leads to: %.1f of the %.1f points this line builds." % (reach[i], total))
            elif direct[i] > 0:
                d.append("That's %.1f of the %.1f points this line builds." % (direct[i], total))
            e["detail"] = d
        else:
            nxt = sorted(feeds[i])
            if nxt: e["for"] = "for step %s" % lst([str(k + 1) for k in nxt[:3]])
        out.append(e)
    return out, round(total, 1)

def last_before(steps, c, i):
    j = None
    for k in range(i):
        if c in made_by(steps[k]): j = k
    return j

def short(t):
    t = t.split(" (")[0]
    return t if len(t) <= 70 else t[:67].rstrip() + "..."
