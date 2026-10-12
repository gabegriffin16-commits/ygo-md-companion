"""Guide Crafter mockup v2 (compact): renders starters.py output as a starter grid + one card per starter (main line as
a short card flow, end board tiles with interruption badges, up to 3 upgrades, handtrap pills). Details stay folded.
  python3 guidepage2.py <starters.json> <deck.json> <out.html> "<deck name>" """
import json, sqlite3, subprocess, sys, html
src, deckf, outf, title = sys.argv[1:5]
D = "C:/Users/gabeg/AppData/Roaming/Master Duel Companion/combo-data/"
ENGINE = "C:/Users/gabeg/AppData/Local/mdc-engine-build/ce-build/Release/combo-engine.exe"
db = sqlite3.connect(D + "cards.cdb")
def row(c): return db.execute("select t.name, t.desc, d.type from datas d join texts t on t.id=d.id where d.id=?", (c,)).fetchone() or (str(c), "", 0)
nm = lambda c: row(c)[0] if c else "a drawn card"
G = json.load(open(src)); dk = json.load(open(deckf))
HT = {14558127: "Ash", 10045474: "Imperm", 97268402: "Veiler", 94145021: "Droll", 27204311: "Nibiru"}
p = subprocess.Popen([ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
q = [10]
def ask(o):
    q[0] += 1; o["id"] = q[0]; p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("ready") or "score" in r or r.get("error")): return r
ask({"cmd": "init", "cdb": D + "cards.cdb", "scripts": D + "CardScripts.zip"})
def why(L):
    return ask({"cmd": "score", "field": L["field"], "zones": L.get("zones", []), "backrow": L["backrow"], "hand": L.get("left", []), "gy": L.get("gy", []), "banished": L.get("banished", []), "extra": dk["extra"], "deck": dk["main"]})["score"]
def stops(L):
    v = {}
    for s in why(L).get("stops", []):
        if s.get("code"): v[s["code"]] = v.get(s["code"], 0) + s["counts"]
    return v
def plain(w):
    """Engine reason text -> words a player reads."""
    for a, b in (("body: ", "on the field: "), ("hand (extender on their turn): ", "in hand, can summon itself on their turn: "), ("hand (starter next turn): ", "in hand, starts next turn: "),
                 ("hand: ", "in hand: "), ("lock: ", "lock: "), ("sturdy: ", "hard to remove: "), ("revive body: ", "can bring back: "), ("backrow: ", "set card: ")):
        if w.startswith(a): return b + w[len(a):]
    return w
def breakdown(L):
    w = why(L); rows = []
    if abs(w["total"] - L["score"]) > 0.05: print("WARNING: breakdown %.2f != line score %.2f (%s)" % (w["total"], L["score"], nm(L["steps"][0][1]) if L["steps"] else "?"))
    for i, s in enumerate(w.get("stops", [])):
        rows.append('<tr><td>%s</td><td class="n">%.1f</td><td class="n">%s</td><td class="n b">%.1f</td></tr>' % (esc(s["what"] or nm(s.get("code"))), s["value"], "&times;%.1f" % (s["counts"] / s["value"]) if s["value"] else "", s["counts"]))
    fl = sorted(w.get("flat", []), key=lambda x: -x["value"])
    for x in [x for x in fl if x["value"] >= 1.0]:   # locks, goals: their own rows
        rows.append('<tr><td>%s</td><td class="n">%.1f</td><td class="n"></td><td class="n b">%.1f</td></tr>' % (esc(plain(x["what"])), x["value"], x["value"]))
    fl = [x for x in fl if x["value"] < 1.0]
    small = sum(x["value"] for x in fl)
    det = ", ".join("%s %.1f" % (esc(plain(x["what"])), x["value"]) for x in fl)
    rows.append('<tr class="small"><td>Small extras <span class="det">(%s)</span></td><td></td><td></td><td class="n b">%.1f</td></tr>' % (det, small))
    return '<details class="more"><summary>Why %.1f points</summary><table class="why"><thead><tr><th>Interruption</th><th class="n">Worth</th><th class="n">Counted</th><th class="n">Points</th></tr></thead><tbody>%s</tbody><tfoot><tr><td>Total</td><td></td><td></td><td class="n b">%.1f</td></tr></tfoot></table></details>' % (w["total"], "".join(rows), w["total"])
def kind(c):
    t = row(c)[1].lower()
    for k, words in (("Negate", ("negate",)), ("Banish", ("banish",)), ("Destroy", ("destroy",)), ("Bounce", ("return", "shuffle")), ("Summon", ("special summon",))):
        if any(w in t for w in words): return k
    return ""
def esc(s): return html.escape(str(s))
def flow(steps, hand):
    out, last = [], None
    for s in steps:
        c, do = s[1], s[0]
        if c == last and do != "Opp": continue
        last = c
        cls = "opp" if do == "Opp" else ("hand" if c in hand else "")
        out.append('<span class="fc %s">%s</span>' % (cls, esc(("They: " if do == "Opp" else "") + nm(c))))
    return '<div class="flow">' + '<span class="ar" aria-hidden="true">&rsaquo;</span>'.join(out) + "</div>"
def steps_list(steps):
    HINT = {501: "discard", 503: "banish", 504: "send", 506: "add", 509: "Special Summon", 510: "Set", 527: "place", 511: "material", 512: "material", 513: "material", 533: "material"}
    li = []
    for s in steps:
        do, c, fx, picks, groups = s
        extra = "; ".join("%s %s" % (HINT.get(h, "pick"), ", ".join(nm(x) for x in cs)) for h, cs in (groups or []) if cs)
        li.append("<li>%s <b>%s</b>%s</li>" % (esc("They activate" if do == "Opp" else do), esc(nm(c)), (" &middot; " + esc(extra)) if extra else ""))
    return '<ol class="steps">' + "".join(li) + "</ol>"
def board(L):
    sv = stops(L)
    tiles = []
    for c in L["field"] + L["backrow"]:
        v = sv.pop(c, 0); k = kind(c) if v > 0.4 else ""
        tiles.append('<div class="tile%s"><span class="tn">%s</span>%s</div>' % (" set" if c in L["backrow"] else "", esc(nm(c)), ('<span class="bd b-%s">%s %.1f</span>' % (k.lower(), k, v)) if k else ""))
    # Cards elsewhere that still count as interruptions: handtraps / Traps held in hand, a monster banished until the End Phase.
    for where, cs, cls in (("in hand", L.get("left", []), "hand"), ("back at End Phase", L.get("banished", []), "ban")):
        for c in cs:
            v = sv.pop(c, 0)
            if v <= 0.4: continue
            k = kind(c)
            tiles.append('<div class="tile %s"><span class="tn">%s</span><span class="wh">%s</span>%s</div>' % (cls, esc(nm(c)), where, ('<span class="bd b-%s">%s %.1f</span>' % (k.lower(), k, v)) if k else ""))
    return '<div class="board">' + "".join(tiles) + "</div>" + breakdown(L)
def pill(v, main):
    if not v["used"]: return '<span class="pill na" title="%s can\'t hit this line">%s</span>' % (HT[v["by"]], HT[v["by"]])
    k = v["keeps"]; cls = "ok" if k >= 0.85 else "mid" if k >= 0.6 else "bad"
    return '<span class="pill %s" title="keeps %d%% of the board">%s</span>' % (cls, round(k * 100), HT[v["by"]])
# Every shown score is the re-scored one, so headlines match their "Why" tables (the search's own can differ by ~0.1).
for s0 in G["starters"]:
    s0["main"]["score"] = why(s0["main"])["total"]
    for u in s0["upgrades"]: u["line"]["score"] = why(u["line"])["total"]; u["gain"] = u["line"]["score"] - s0["main"]["score"]
    s0["upgrades"] = [u for u in s0["upgrades"] if u["gain"] > 0.05]
starters = sorted(G["starters"], key=lambda s: -(s["reliability"] * 10 + s["main"]["score"]))
grid, cards = [], []
for i, s in enumerate(starters):
    m = s["main"]; rel = round(s["reliability"] * 10)
    pills = "".join(pill(v, m) for v in s["vs"])
    grid.append('<a class="st" href="#s%d"><span class="sn">%s%s</span><span class="meter" aria-label="good board in %d of 10 hands">%s</span><span class="sc">%.1f</span><span class="pills">%s</span></a>' % (
        i, esc(nm(s["card"])), ('<span class="k">%s</span>' % s["kind"]) if s.get("kind") else "", rel, "".join('<i class="%s"></i>' % ("on" if k < rel else "") for k in range(10)), m["score"], pills))
    others = [c for c in m["uses"] if c != s["card"]]
    # A 1-card line that spends a non-engine card (a handtrap as material or a cost): any such card works, say so.
    fill = [c for c in others if c in s.get("soloFiller", [])] if s.get("kind") == "1-card" else []
    eng = [c for c in others if c not in fill]
    plus = ("+ " + ", ".join(nm(c) for c in eng)) if eng else "on its own"
    if fill: plus += " (uses %s from your hand: any spare monster works)" % " and ".join(sorted(set(nm(c) for c in fill)))
    # Upgrades found in the same hand are one upgrade (every card of that hand got the same gain); handtraps aren't upgrades.
    merged = {}
    for u in s["upgrades"]:
        if u["card"] in HT or (row(u["card"])[2] & 0x4): continue
        k = json.dumps(u["line"]["steps"])
        if k in merged: merged[k]["cards"].append(u["card"])
        else: merged[k] = dict(u, cards=[u["card"]])
    s["upgrades"] = [dict(u, card=None) for u in merged.values()]
    ups = "".join('<details class="up"><summary><span class="uc">+ %s</span><span class="ug">+%.1f</span></summary>%s%s</details>' % (esc(" + ".join(nm(c) for c in u["cards"])), u["gain"], flow(u["line"]["steps"], u["line"]["hand"]), board(u["line"])) for u in s["upgrades"])
    vsd = "".join('<details class="vs"><summary>%s <span class="vk">keeps %d%%</span></summary>%s</details>' % (pill(v, m), round(v["keeps"] * 100), flow(v["line"]["steps"], v["line"]["hand"])) for v in s["vs"] if v["used"] and v["line"])
    cards.append('''<details class="card" id="s%d"%s><summary><span class="cn">%s</span><span class="cm">%sgood board in %d of 10 hands &middot; typical %.1f</span><span class="cs">%.1f</span></summary>
<div class="body">
  <div class="sec"><h3>Main line <small>%s</small></h3>%s<details class="more"><summary>All steps</summary>%s</details></div>
  <div class="sec"><h3>End board</h3>%s</div>
  %s
  <div class="sec"><h3>If they have</h3><div class="pills big">%s</div>%s</div>
</div></details>''' % (i, " open" if i == 0 else "", esc(nm(s["card"])), ("%s starter &middot; " % s["kind"]) if s.get("kind") else "", rel, s["median"], m["score"], esc(plus), flow(m["steps"], m["hand"]), steps_list(m["steps"]), board(m),
        ('<div class="sec"><h3>Upgrades</h3>%s</div>' % ups) if ups else "", pills, vsd))
o = G["opening"]
# The rest of the deck: extenders and situational cards (compact entries), then non-engine cards. Every card appears once.
def entry(c):
    k = c["card"]; b = c.get("best")
    what = "used in %d%% of its hands, opens the line in %d%% &middot; typical board with it %.1f" % (round(c["used"] * 100), round(c["opens"] * 100), c["median"])
    inner = ('<div class="sec"><h3>Its best line <small>%.1f &middot; hand: %s</small></h3>%s%s</div>' % (why(b)["total"], esc(", ".join(nm(x) for x in b["hand"])), flow(b["steps"], b["hand"]), board(b))) if b else '<p class="cm">No line used it.</p>'
    return '<details class="card mini"><summary><span class="cn">%s <small>&times;%d</small></span><span class="cm">%s</span><span class="cs">%.1f</span></summary><div class="body">%s</div></details>' % (esc(nm(k)), c["copies"], what, c["median"], inner)
rest = []
for role, head, note in (("extender", "Extenders", "Cards the lines use but don't open with: they make a starter's line bigger."),
                         ("situational", "Situational", "Cards the lines use in under half of their hands.")):
    cs = [c for c in G.get("cards", []) if c["role"] == role]
    if cs: rest.append('<h2 class="part">%s <small>%s</small></h2>%s' % (head, note, "".join(entry(c) for c in sorted(cs, key=lambda c: -c["used"]))))
if G.get("nonEngine"):
    rest.append('<h2 class="part">Non-engine <small>Handtraps and Traps: not part of the combo; the score counts them as interruptions when held.</small></h2><p class="ne">%s</p>' % esc(", ".join(nm(c) for c in G["nonEngine"])))
ncards = len(G.get("cards", [])); nst = len(starters)
cover = ("Covers all %d cards in the Main Deck: %d starters, %d other engine cards, %d non-engine." % (ncards + len(G.get("nonEngine", [])), nst, ncards - nst, len(G.get("nonEngine", [])))) if G.get("cards") else ""
page = '''<title>%s Guide Draft</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Condensed:wght@600;700&display=swap">
<style>
/* Layout: summary first (a grid of starters), one folded card per starter; the app's navy / gold palette */
:root { --bg:#EFECFA; --panel:#FFFFFF; --panel2:#E4DFF7; --ink:#1B1640; --muted:#5b5490; --gold:#B07A00; --red:#C21F37; --sky:#1A7FB5; --line:#cfc8ee; --ok:#2E8B57; --mid:#B8860B;
  --display:"Barlow Condensed","Arial Narrow",sans-serif; --body:"Barlow",system-ui,sans-serif; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#0A1630; --panel:#11214A; --panel2:#182C5C; --ink:#EEF4FF; --muted:#A3B5DD; --gold:#F2B632; --red:#E2384D; --sky:#6EC8F5; --line:#223A72; --ok:#4CC38A; --mid:#E8B84A; color-scheme: dark } }
:root[data-theme="dark"] { --bg:#0A1630; --panel:#11214A; --panel2:#182C5C; --ink:#EEF4FF; --muted:#A3B5DD; --gold:#F2B632; --red:#E2384D; --sky:#6EC8F5; --line:#223A72; --ok:#4CC38A; --mid:#E8B84A; color-scheme: dark }
body { background:var(--bg); color:var(--ink); font-family:var(--body); font-size:15px; line-height:1.45; }
.wrap { max-width:1000px; margin:0 auto; padding-inline:18px; padding-block:18px 60px; }
.top { border-bottom:2px solid var(--gold); padding-bottom:10px; margin-bottom:14px; display:flex; flex-wrap:wrap; justify-content:space-between; align-items:flex-end; gap:10px; }
h1 { font-family:var(--display); font-size:32px; margin:0; letter-spacing:.4px; }
h1 small { color:var(--gold); font-size:16px; letter-spacing:2px; text-transform:uppercase; margin-left:8px; }
.sum { color:var(--muted); margin:4px 0 0; }
.badge { font-size:11px; letter-spacing:1.5px; text-transform:uppercase; border:1px solid var(--gold); color:var(--gold); padding:3px 9px; border-radius:999px; }
.grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(280px,1fr)); gap:8px; margin-bottom:18px; }
.st { display:grid; grid-template-columns:minmax(0,1fr) auto; grid-template-areas:"n s" "m p"; gap:4px 10px; padding:10px 12px; background:var(--panel); border:1px solid var(--line); border-radius:10px; color:var(--ink); text-decoration:none; }
.st:hover, .st:focus-visible { border-color:var(--gold); outline:none; }
.sn { grid-area:n; font-weight:600; line-height:1.25; }
.sc { grid-area:s; font-family:var(--display); font-size:22px; color:var(--gold); font-variant-numeric:tabular-nums; line-height:1; }
.meter { grid-area:m; display:flex; gap:2px; align-items:center; }
.meter i { width:9px; height:9px; border-radius:2px; background:var(--panel2); }
.meter i.on { background:var(--gold); }
.st .pills { grid-area:p; }
.pills { display:flex; gap:3px; flex-wrap:wrap; }
.pill { font-size:10.5px; padding:1px 6px; border-radius:999px; border:1px solid var(--line); color:var(--muted); }
.pill.ok { border-color:var(--ok); color:var(--ok); } .pill.mid { border-color:var(--mid); color:var(--mid); } .pill.bad { border-color:var(--red); color:var(--red); } .pill.na { opacity:.55; }
.pills.big .pill { font-size:12.5px; padding:2px 9px; }
h2.part { font-family:var(--display); font-size:22px; margin:22px 0 8px; } h2.part small { font-family:var(--body); font-size:13px; color:var(--muted); font-weight:400; margin-left:8px; }
.k { display:inline-block; margin-left:6px; font-size:10.5px; font-weight:400; color:var(--muted); border:1px solid var(--line); border-radius:4px; padding:0 4px; vertical-align:2px; }
.cover { color:var(--muted); margin:0 0 4px; } .ne { margin:0; } .cn small { color:var(--muted); font-size:14px; }
details.card.mini .cn { font-size:18px; } details.card.mini .cs { font-size:20px; }
details.card { background:var(--panel); border:1px solid var(--line); border-radius:12px; margin-bottom:10px; }
details.card > summary { display:grid; grid-template-columns:minmax(0,1fr) auto; grid-template-areas:"n s" "m s"; gap:2px 12px; padding:12px 14px; cursor:pointer; list-style:none; }
details.card > summary::-webkit-details-marker { display:none; }
details.card > summary:focus-visible, details summary:focus-visible { outline:2px solid var(--gold); outline-offset:2px; }
.cn { grid-area:n; font-family:var(--display); font-size:21px; letter-spacing:.3px; }
.cm { grid-area:m; color:var(--muted); font-size:13px; }
.cs { grid-area:s; align-self:center; font-family:var(--display); font-size:26px; color:var(--gold); font-variant-numeric:tabular-nums; }
.body { padding:0 14px 12px; display:grid; gap:12px; }
.sec h3 { font-size:11.5px; letter-spacing:1.4px; text-transform:uppercase; color:var(--muted); margin:0 0 6px; font-weight:600; }
.sec h3 small { text-transform:none; letter-spacing:0; color:var(--ink); font-size:12.5px; margin-left:6px; font-weight:400; }
.flow { display:flex; flex-wrap:wrap; align-items:center; gap:4px; }
.fc { padding:3px 8px; border-radius:6px; background:var(--panel2); font-size:13.5px; }
.fc.hand { box-shadow:inset 0 0 0 1px var(--gold); }
.fc.opp { background:transparent; box-shadow:inset 0 0 0 1px var(--red); color:var(--red); }
.ar { color:var(--muted); }
table.why { border-collapse:collapse; margin-top:6px; font-size:13px; width:100%%; max-width:560px; }
.why th { text-align:left; font-weight:600; color:var(--muted); font-size:11.5px; letter-spacing:.6px; text-transform:uppercase; border-bottom:1px solid var(--line); padding:3px 6px; }
.why td { padding:3px 6px; border-bottom:1px solid var(--line); vertical-align:top; }
.why .n { text-align:right; font-variant-numeric:tabular-nums; white-space:nowrap; } .why .b { font-weight:600; }
.why tfoot td { border-bottom:none; font-weight:600; } .why .det { color:var(--muted); font-size:12px; }
.rub { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:2px 14px 10px; margin-bottom:14px; }
.rub > summary { cursor:pointer; padding:10px 0 6px; font-weight:600; }
.rub p { margin:6px 0; } .rub ul { margin:4px 0 8px; padding-left:20px; } .rub li { margin:2px 0; }
.scale { display:grid; grid-template-columns:auto 1fr; gap:3px 12px; font-size:13.5px; margin:6px 0; } .scale b { color:var(--gold); font-variant-numeric:tabular-nums; }
details.more summary, details.up summary, details.vs summary { cursor:pointer; color:var(--sky); font-size:13px; margin-top:6px; }
.steps { margin:6px 0 0; padding-left:24px; display:grid; gap:3px; font-size:13.5px; }
.steps li::marker { color:var(--gold); }
.board { display:flex; flex-wrap:wrap; gap:6px; }
.tile { display:flex; flex-direction:column; gap:4px; min-width:110px; max-width:170px; padding:7px 9px; border-radius:8px; background:var(--bg); border:1px solid var(--line); font-size:13px; }
.tile.set { border-style:dashed; } .tile.hand, .tile.ban { border-style:dotted; opacity:.9; } .wh { font-size:11px; color:var(--muted); }
.tn { line-height:1.25; }
.bd { align-self:flex-start; font-size:10.5px; letter-spacing:.8px; text-transform:uppercase; padding:1px 6px; border-radius:4px; background:var(--panel2); }
.b-negate { color:var(--gold); } .b-banish, .b-destroy, .b-bounce { color:var(--sky); } .b-summon { color:var(--muted); }
details.up summary { display:flex; gap:8px; align-items:baseline; color:var(--ink); }
.uc { font-weight:600; } .ug { color:var(--ok); font-variant-numeric:tabular-nums; font-size:13px; }
details.up > .flow, details.up > .board, details.vs > .flow { margin:6px 0 4px; }
.vk { color:var(--muted); font-size:12.5px; margin-left:4px; }
</style>
<div class="wrap">
  <div class="top"><div><h1>%s <small>Guide draft</small></h1>
    <p class="sum">From %d random opening hands: typical board %.1f; a strong board (%.1f+) in %d of 10. Each starter below is checked over its own hands.</p></div><span class="badge">Mockup v2</span></div>
  <details class="rub" open><summary>How the score works</summary>
    <p>The score counts how hard your end board is for the opponent to play through on their turn. Most points come from <b>interruptions</b>: cards that can stop one of their plays.</p>
    <div class="scale"><b>3.0</b><span>Negate (stops a card or effect outright). +0.5 if it hits any card or effect; less if it's limited (Spells/Traps only &minus;0.7, only stops destruction &minus;1.5)</span>
      <b>2.5</b><span>Banish or take one of their cards, or a lock that stops them summoning / activating</span>
      <b>2.0</b><span>Destroy, bounce, or send one of their cards to the GY; Droll &amp; Lock Bird</span>
      <b>1.5</b><span>Narrower stops: flip face-down, "can't activate" one kind of card, locks that only cover one Type</span>
      <b>0.5</b><span>Battle-only (negate an attack); reviving a monster (plus what the revived monster can do)</span></div>
    <ul><li>+0.3 if it doesn't target (gets past protection). A summon on their turn is worth what it can actually bring out.</li>
      <li>Your best interruption counts in full, the next at 90%%, then 80%%... down to 50%%. More stops still help, but each one adds a bit less.</li>
      <li>Small extras: each monster on the field 0.5 (0.8 from the Extra Deck), a card left in hand 0.3&ndash;0.8, hard-to-remove monsters +0.6.</li></ul>
    <p>Rough scale: <b>~6</b> = one or two real stops, <b>~10</b> = three, <b>14+</b> = four or more. Here a <b>strong board is %.1f+</b>: what the best quarter of random opening hands reached. Every end board has a "Why N points" table that adds up to its score.</p>
    <ul><li><b>Bar of 10</b> on a starter: in how many of 10 random hands with that card you reach a strong board.</li>
      <li><b>Handtrap pills</b>: green = the board keeps 85%%+ of its points if they play it, gold = 60&ndash;85%%, red = less; faded = it can't hit that line. Tap one under "If they have" for the line that plays through it.</li>
      <li><b>Roles</b>: each engine card is tested alone, with the deck's most common non-engine cards filling the hand (they can still be used as material or a cost, as in a real hand). If the board it builds scores %.0f+ (not counting cards left in hand), it's a <b>1-card starter</b> and that line is its main line. Cards the lines usually open with but that need another engine card are <b>2-card starters</b>. Cards used in half or more of their hands without opening are <b>extenders</b>; the rest are <b>situational</b>.</li>
      <li><b>Line chips</b>: gold outline = a card from your hand, red = the opponent's handtrap. <b>Board tiles</b>: solid = monster, dashed = set card, dotted = held in hand or back at the End Phase.</li></ul>
  </details>
  <p class="cover">%s</p>
  <h2 class="part">Starters <small>1-card: makes a board on its own. 2-card: needs a partner card.</small></h2>
  <nav class="grid" aria-label="Starters">%s</nav>
  %s
  %s
</div>''' % (esc(title), esc(title), o["hands"], o["median"], o["good"], round(o["shareGood"] * 10), o["good"], G.get("soloMin", 4), esc(cover), "".join(grid), "".join(cards), "".join(rest))
open(outf, "w", encoding="utf-8").write(page)
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
print("wrote", outf, len(starters), "starters")
