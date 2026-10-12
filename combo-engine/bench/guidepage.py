"""Guide Crafter mockup: renders one deck's generated lines (app generation output, userData/generated/<deck>.json) as a
guide page. python3 guidepage.py <generated.json> <deck.json> <out.html> "<deck name>"  (page output goes to .mockups/, untracked)"""
import json, sqlite3, subprocess, sys, html
gen_f, deck_f, out_f, title = sys.argv[1:5]
D = "C:/Users/gabeg/AppData/Roaming/Master Duel Companion/combo-data/"
ENGINE = "C:/Users/gabeg/AppData/Local/mdc-engine-build/ce-build/Release/combo-engine.exe"
db = sqlite3.connect(D + "cards.cdb")
nm = lambda c: (db.execute("select name from texts where id=?", (c,)).fetchone() or [str(c)])[0] if c else "a drawn card"
g = json.load(open(gen_f)); d = json.load(open(deck_f))
HINT = {500: "Tribute", 501: "discard", 502: "destroy", 503: "banish", 504: "send to the GY", 505: "return to the hand", 506: "add",
        507: "shuffle into the Deck", 508: "Normal Summon", 509: "Special Summon", 510: "Set", 518: "equip", 519: "detach", 527: "place", 531: "Tribute", 578: "attach"}
MAT = {511, 512, 513, 533}
HT = {14558127: "Ash Blossom", 10045474: "Infinite Impermanence", 97268402: "Effect Veiler", 94145021: "Droll & Lock Bird", 27204311: "Nibiru"}
p = subprocess.Popen([ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
q = [10]
def ask(o):
    q[0] += 1; o["id"] = q[0]; p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("ready") or "score" in r or r.get("error")): return r
ask({"cmd": "init", "cdb": D + "cards.cdb", "scripts": D + "CardScripts.zip"})
def why(L):
    r = ask({"cmd": "score", "field": L["f"], "zones": L.get("z", []), "backrow": L["b"], "hand": L.get("l", []), "extra": d["extra"], "deck": d["main"]})["score"]
    return [(s["what"], s["value"]) for s in r.get("stops", [])]
def esc(s): return html.escape(str(s))
def step_text(st):
    do, card, fx, picks, groups = st
    n = nm(card)
    if do == "Normal Summon": return "Normal Summon <b>%s</b>" % esc(n), []
    if do == "Opp": return "They activate <b>%s</b>" % esc(n), []
    clauses, got = [], []
    for h, cards in groups or []:
        ns = [nm(c) for c in cards]
        if not ns: continue
        if h in MAT: clauses.append("using " + ", ".join(ns) + " as material")
        else: clauses.append("%s %s" % (HINT.get(h, "choose"), ", ".join(ns)))
        if h in (506, 509, 510, 527): got += cards
    if do == "Special Summon":
        return "Special Summon <b>%s</b>%s" % (esc(n), (" (" + esc("; ".join(clauses)) + ")") if clauses else ""), got + [card]
    fx = (fx or "").strip().rstrip(".")
    body = "; ".join(clauses) if clauses else (fx[:70] if fx else "use its effect")
    return "<b>%s</b>: %s" % (esc(n), esc(body)), got
def steps_html(st, board):
    rows, used = [], {}
    for i, s in enumerate(st):
        for c in [s[1]]:
            used.setdefault(c, []).append(i)
    for i, s in enumerate(st):
        t, got = step_text(s)
        notes = []
        for c in got:
            later = [j for j in used.get(c, []) if j > i]
            if later: notes.append("%s is used at step %d" % (nm(c), later[0] + 1))
            elif c in board: notes.append("%s stays for the end board" % nm(c))
        opp = s[0] == "Opp"
        rows.append('<li class="%s"><span class="txt">%s</span>%s</li>' % ("opp" if opp else "", t, ('<span class="why">' + esc("; ".join(notes)) + "</span>") if notes else ""))
    return '<ol class="steps">' + "".join(rows) + "</ol>"
def chips(cards, cls=""): return "".join('<span class="chip %s">%s</span>' % (cls, esc(nm(c))) for c in cards)
lines = sorted(g["lines"], key=lambda L: -L["s"])
sections = []
for k, L in enumerate(lines):
    board = set(L["f"]) | set(L["b"])
    stops = why(L)
    stop_html = "".join('<li><span class="val">%.1f</span>%s</li>' % (v, esc(w)) for w, v in stops[:6])
    choke = sorted([b for b in L.get("fb", []) if b["s"] < L["s"] - 3], key=lambda b: b["s"])[:3]
    choke_html = "".join('<li><span class="ht">%s</span> on step %d &rarr; best left %.1f</li>' % (esc(HT.get(b["by"], nm(b["by"]))), b["i"] + 1, b["s"]) for b in choke)
    vs_html = ""
    for v in sorted(L.get("vs", []), key=lambda v: list(HT).index(v["by"]) if v["by"] in HT else 9):
        delta = v["s"] - L["s"]
        vs_html += '<details class="vs"><summary><span class="ht">%s</span><span class="vsc">%.1f</span><span class="dlt %s">%+.1f</span><span class="vsb">%s</span></summary>%s</details>' % (
            esc(HT.get(v["by"], nm(v["by"]))), v["s"], "down" if delta < -1 else "ok", delta, esc(", ".join(nm(c) for c in v["f"])) or "no monsters", steps_html(v["st"], set(v["f"]) | set(v["b"])))
    sections.append('''
<section class="combo" id="c%d">
  <header><div class="hand"><span class="lbl">Starting hand</span>%s</div><div class="score"><span class="lbl">Board</span><b>%.1f</b></div></header>
  <div class="grid">
    <div class="col-steps"><h3>Line</h3>%s</div>
    <div class="col-side">
      <div class="panel"><h3>End board</h3><div class="zone"><span class="lbl">Field</span>%s</div><div class="zone"><span class="lbl">Set</span>%s</div>%s
        <h4>Why it holds up</h4><ul class="stops">%s</ul></div>
      %s
      <div class="panel"><h3>If they have&hellip;</h3><p class="hint">Lines played knowing they'll use it at the first chance.</p>%s</div>
    </div>
  </div>
</section>''' % (k, chips(L["h"], "hand"), L["s"], steps_html(L["st"], board), chips(L["f"]) or '<span class="none">none</span>', chips(L["b"]) or '<span class="none">none</span>',
       ('<div class="zone"><span class="lbl">Hand</span>%s</div>' % chips(L.get("l", []))) if L.get("l") else "", stop_html,
       ('<div class="panel"><h3>Choke points</h3><ul class="choke">%s</ul></div>' % choke_html) if choke_html else "", vs_html or '<p class="none">No handtrap could hit this line.</p>'))
toc = "".join('<a href="#c%d"><span>%s</span><b>%.1f</b></a>' % (k, esc(" + ".join(nm(c) for c in L["h"])), L["s"]) for k, L in enumerate(lines))
page = '''<title>%s Guide Draft</title>
<style>
/* Layout: one column of combos, each a two-column card (line | board + handtrap answers); the app's navy / gold palette */
:root { --bg:#EFECFA; --panel:#FFFFFF; --panel2:#E4DFF7; --ink:#1B1640; --muted:#5b5490; --gold:#B07A00; --red:#C21F37; --sky:#1A7FB5; --line:#cfc8ee;
  --display: "Barlow Condensed", "Arial Narrow", sans-serif; --body: "Barlow", system-ui, sans-serif; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#0A1630; --panel:#11214A; --panel2:#182C5C; --ink:#EEF4FF; --muted:#A3B5DD; --gold:#F2B632; --red:#E2384D; --sky:#6EC8F5; --line:#223A72; color-scheme: dark } }
:root[data-theme="dark"] { --bg:#0A1630; --panel:#11214A; --panel2:#182C5C; --ink:#EEF4FF; --muted:#A3B5DD; --gold:#F2B632; --red:#E2384D; --sky:#6EC8F5; --line:#223A72; color-scheme: dark }
body { background: var(--bg); color: var(--ink); font-family: var(--body); font-size: 15px; line-height: 1.45; }
.wrap { max-width: 1180px; margin: 0 auto; padding-inline: 20px; padding-block: 18px 60px; }
.top { display:flex; flex-wrap:wrap; justify-content:space-between; align-items:flex-end; gap:12px; border-bottom: 2px solid var(--gold); padding-bottom: 12px; }
h1 { font-family: var(--display); font-weight: 700; font-size: 34px; letter-spacing: .5px; margin: 0; text-wrap: balance; }
h1 small { color: var(--gold); font-size: 18px; margin-left: 8px; letter-spacing: 2px; text-transform: uppercase; }
.note { color: var(--muted); max-width: 62ch; margin: 6px 0 0; }
.badge { font-size: 12px; letter-spacing: 1.5px; text-transform: uppercase; border: 1px solid var(--gold); color: var(--gold); padding: 4px 10px; border-radius: 999px; }
.toc { display:grid; grid-template-columns: repeat(auto-fill, minmax(230px, 1fr)); gap: 8px; margin: 16px 0 22px; }
.toc a { display:flex; justify-content:space-between; gap:10px; padding: 9px 12px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; color: var(--ink); text-decoration:none; }
.toc a:hover, .toc a:focus-visible { border-color: var(--gold); outline: none; }
.toc b, .score b { font-variant-numeric: tabular-nums; color: var(--gold); }
.combo { background: var(--panel); border: 1px solid var(--line); border-radius: 12px; padding: 16px; margin-bottom: 18px; }
.combo header { display:flex; flex-wrap:wrap; justify-content:space-between; align-items:center; gap: 10px; border-bottom: 1px solid var(--line); padding-bottom: 10px; margin-bottom: 12px; }
.lbl { font-size: 11px; letter-spacing: 1.4px; text-transform: uppercase; color: var(--muted); margin-right: 8px; }
.score b { font-family: var(--display); font-size: 26px; }
.chip { display:inline-block; padding: 3px 9px; margin: 2px 4px 2px 0; border-radius: 6px; background: var(--panel2); font-size: 13.5px; }
.chip.hand { border: 1px solid var(--gold); }
.none { color: var(--muted); font-size: 13.5px; }
.grid { display:grid; grid-template-columns: minmax(0, 1.35fr) minmax(0, 1fr); gap: 18px; }
@media (max-width: 820px) { .grid { grid-template-columns: minmax(0,1fr); } }
h3 { font-family: var(--display); font-size: 19px; letter-spacing: .5px; margin: 0 0 8px; }
h4 { font-size: 12px; letter-spacing: 1.4px; text-transform: uppercase; color: var(--muted); margin: 12px 0 6px; }
.steps { margin: 0; padding-left: 26px; display:grid; gap: 6px; }
.steps li { padding-left: 4px; }
.steps li::marker { color: var(--gold); font-variant-numeric: tabular-nums; }
.steps li.opp .txt { color: var(--red); }
.why { display:block; color: var(--sky); font-size: 13px; }
.panel { background: var(--bg); border: 1px solid var(--line); border-radius: 10px; padding: 12px; margin-bottom: 12px; }
.zone { margin-bottom: 4px; }
.stops, .choke { list-style:none; margin:0; padding:0; display:grid; gap:4px; font-size: 13.5px; }
.val { display:inline-block; min-width: 2.6em; font-variant-numeric: tabular-nums; color: var(--gold); font-weight: 600; }
.ht { font-weight: 600; }
.hint { color: var(--muted); font-size: 13px; margin: -4px 0 8px; }
details.vs { border-top: 1px solid var(--line); }
details.vs summary { display:grid; grid-template-columns: 9.5em 3em 3.5em minmax(0,1fr); gap: 8px; align-items: baseline; padding: 7px 2px; cursor: pointer; font-size: 13.5px; }
details.vs summary:focus-visible { outline: 2px solid var(--gold); outline-offset: 2px; }
.vsc { font-variant-numeric: tabular-nums; font-weight: 600; }
.dlt { font-variant-numeric: tabular-nums; font-size: 12.5px; }
.dlt.down { color: var(--red); } .dlt.ok { color: var(--muted); }
.vsb { color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
details.vs .steps { margin: 4px 0 10px; font-size: 13.5px; }
</style>
<div class="wrap">
  <div class="top"><div><h1>%s <small>Guide draft</small></h1>
    <p class="note">Mockup for the Guide Crafter page, built from the engine's real output for %d starting hands. Board numbers are the engine's score (higher holds up better on their turn).</p></div>
    <span class="badge">Mockup</span></div>
  <nav class="toc" aria-label="Combos">%s</nav>
  %s
</div>''' % (esc(title), esc(title), len(lines), toc, "".join(sections))
open(out_f, "w", encoding="utf-8").write(page)
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
print("wrote", out_f, len(lines), "combos")
