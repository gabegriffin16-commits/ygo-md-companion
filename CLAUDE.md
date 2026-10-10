# Master Duel Companion App: handoff notes for Claude

Read this first. It's what a new session (Claude Code on the owner's PC, or a cloud session) needs to pick up the work.
The owner (the user) owns the project and the GitHub repo `gabegriffin16-commits/ygo-md-companion`. Pull before working; another
Claude session may be working in the same repo, so keep commits small and messages descriptive.

## Working with the owner

- Casual, direct tone. Results over narration.
- **Check with him before UI or design changes** you decide on yourself, and don't assume when the request is unclear:
  ask. Concrete requests they make: just do them.
- **Measure alignment and centering before pushing** (pixel checks in Playwright). They've called out off-center
  icons as "a simple catch" that shouldn't slip through.
- Don't build release zips unless they ask.
- 1920x1080 is the main layout; compact view (~540px wide) matters too.
- Nothing in the UI should say "admin" or draw attention to their admin role (admin-only things just appear for them).
- Fixed details they chose: rarity colors (UR pastel rainbow, SR `#E0B85E`, R `#86BEDB`, N grey); the stop box label
  is "They fucked me".
- Website-only changes need no app release. App changes ship by bumping `version` in `app/package.json`.

## Two sessions at once

The owner may run two Claude sessions on this repo: a cloud session for UI/app work and Claude Code on their PC for
combo-engine work. To stay out of each other's way:
- **Lanes**: UI session owns `index.html` styling/layout, `app/` overlay/settings/reader. Engine session owns
  `combo-engine/`, the engine/generation parts of `app/main.js`, and the combo/generate-lines logic in `index.html`.
  Crossing lanes is fine for small, necessary edits.
- `git pull --rebase` before starting anything and again right before pushing; small commits, push often.
  `index.html` is one big file: keep edits local to the section you're working in so git can merge.
- **Only one session bumps the app version at a time.** Before bumping, pull and check that the latest commit isn't
  already a bump; if both changed `app/`, one release can carry both.
- Leave a line in "Open items" below for anything the other session needs to know.

## What's here

| Path | What it is |
|---|---|
| `index.html` | The whole site/app UI (single file). `startCompanion(DECKDATA, RULES, CARDDB)` builds everything. |
| `deck.json`, `rules.json` | The built-in Omni HERO deck: hand-written lines, play rules, checkpoints. |
| `cards.json` | Card database for the page (from YGOPRODeck; `update_card_db.py`). |
| `app/` | Electron overlay app (`main.js`, `preload.js`, screen reader `reader*.js`, `handreader.js`, `hudreader.js`). |
| `combo-engine/` | The combo finder helper (C++, AGPL-3.0, separate program). See its README for the protocol. |
| `supabase/schema.sql` | Accounts, decks, engine-test tables + row-level security. The owner runs it in the Supabase SQL editor. |
| `tools/` | Test mocks/bridges and `benchtool.py` (see Testing). |
| `.github/workflows/build-app.yml` | Builds the Windows app + helper, smoke-tests the helper, publishes releases. |
| `.github/workflows/engine-bench.yml` | Deep-search test on GitHub runners; triggered by changing `combo-engine/bench/run.json`. |

Site: GitHub Pages from `main`. App: portable exe from GitHub Releases; it checks `releases/latest` and offers updates.

### Page modules (in index.html)
- `MDC`: decks in localStorage, scoped per account (`MDC.scope(uid, isAdmin)`; `MDC.heroAllowed()` is the admin flag).
  Decks: `{id, name, main:[{id,n}], extra, notes, starters, lines, goals, gen, shared}`. `MDC.lines/setLines` = saved lines.
- `MDA`: card-text analyzer (roles: starter/extender/handtrap/engine; Ash/Imperm/Droll tags; opening odds).
- `CLOUD`: Supabase (project `hopdbvllzumhywaxerno`, publishable key in the file). Usernames map to
  `<name>@players.mdc-app.com`. Includes `benchJob/benchPost` for engine tests.
- Combo finder UI: `comboFinder()` (Duel tab opening hand + builder Draw simulator), `cfStepText` (step wording from pick
  "groups" / hint ids), saved lines in the Lines tab.
- Guided tour: `window.startTour()` (spotlight + card; app vs website, compact). Auto-starts once after sign-up
  (`mdc-tour-new`); re-run from Settings (app) or the account menu (website). It makes a temporary "Tutorial deck"
  (id `tour-…`, Omni HERO list, local only: never pushed to the cloud), pilots it to show the deck tools, then deletes it
  and switches back. Deck switches reload, so the tour resumes from sessionStorage `mdc-tour-run`; a leftover tour deck
  is removed on the next load. Steps have segments pre / deck / post. Add a step when a main feature is added.
- Generated lines: `genBox` (builder > Analysis: goals chips + Generate), `paintGen` (Lines / Checkpoints / Counter Guide
  for decks with `deck.gen`), `genEvaluate` (Duel tab best line from the read hand). `deckGoals/goalSuggest` = end-board goals.

### App (app/main.js)
- Combo helper process (`comboStart/comboRequest`), data download to userData `combo-data/` (ProjectIgnis CardScripts zip
  + BabelCDB `cards.cdb`, refreshed weekly).
- IPC: `combo:search/stop`, `gen:start/status/take/cancel` (background generation, result files in userData `generated/`),
  `bench:run/stop` (engine test plans; can use the `engine-test` pre-release helper build).
- Overlay bar/tray, settings, hotkeys, updater, screen reader.

## Combo engine (combo-engine/)

Runs turn one in the real rules engine (edo9300/ygopro-core pinned at `38d04c9f`, Lua 5.4) with ProjectIgnis scripts.
Key parts of `src/main.cpp`:
- **Search**: beam search (widening, parent-diverse) + depth-first workers sharing results. Chain windows with
  optional activations are decision points (Quick-Effect decks combo inside chains).
- **Board score** (`score_of` + `src/evaluate.h`): reads each card's text for interruptions usable on the opponent's turn
  (negate > banish/control > destroy/bounce/send), where they work from (field / set / hand / GY), locks, sturdiness,
  zone requirements (center Main Monster Zone, "switch into the center"), set-able Traps in hand count as set.
  Revival: cards that Special Summon from the GY on the opponent's turn (June Pride, Strelitzia, Rhapsodia, Call of the
  Haunted) let the best matching GY monster (by quoted name / Level cap) count as an extra stop.
  Fusion on their turn (set Favorite Contact): counts only if the Extra Deck has a Fusion whose quoted materials are in
  reach (hand/field/GY/banished, per the card); then it's worth that Fusion (Quick Effect or on-summon effect, e.g.
  Shining Neos Wingman's destroy) and its goal +8. Nothing makeable = dead card.
  End-board goals (`targets`) add +8 each. `{"cmd":"eval","cards":[...]}` shows what it read from cards;
  `{"cmd":"score", field/zones/backrow/hand/gy/banished/extra/targets}` scores any board with a per-card breakdown.
  Rules tuned against guide boards: summoning itself is no stop; a generic opponent's-turn summon is 1.5, a revival 0.5
  (+ what it brings back, chains included); negates: "a card or effect" +0.5, Spell/Trap-only -0.7, "would destroy" -1.5;
  attack-only locks 0.5. End hand: handtraps are stops, Traps/Quick-Plays count as set, an extender that summons itself
  from hand on their turn 0.8, a next-turn starter (searches the Deck) 0.6, anything else 0.3.
- **Zones**: only for decks whose text mentions the center zone/columns (`g_zones`); zone-aware cards go center or side
  by rule; Normal/Extra Deck summons of them branch both ways.
- **Draws** come from 12 blank stand-ins (Spiral Serpent) on top of the Deck, hidden in output (shown as code 0).
- **Handtrap backups**: `oppHand` + `prefix` (choice labels of a line) + `hitCard/hitStep` replays a line and chains
  Ash/Imperm/Veiler/Droll at that step.
- **Speed**: finished duels are reset and reused (`src/reset.cpp`, ~2x); bytecode stripped; mimalloc.
- Known limits: opponent never acts except the tested handtrap; Spell/Trap zones always first free; revival reads
  only the quoted name and Level cap (not Type/Attribute) and ignores once-per-turn names; scoring is generic, so goals
  matter for long combos.

Build (Linux): `cmake -B build -G Ninja -DOCGCORE_DIR=<ygopro-core w/ lua submodule> -DDEPS_DIR=<json.hpp, miniz/, sqlite/> -DMIMALLOC_DIR=<mimalloc>`
then `cmake --build build`. CI shows the exact downloads. Data for tests: CardScripts zip from
`https://codeload.github.com/ProjectIgnis/CardScripts/zip/refs/heads/master`, `cards.cdb` from BabelCDB.

## Testing

- **Reference boards** (tuning the generator toward guide-quality plays): `combo-engine/bench/refs/<deck>.json` holds
  boards from guides plus the engine's picks, with sources. `bench/score.py <engine> <cdb> <zip> refs/<deck>.json`
  prints each board's score and why; the guide board should rank at or near the top, and bench.py runs show what the
  search actually reaches.
- **Local build on the owner's PC**: VS 2022 Build Tools + CMake are installed; deps live in
  `%LOCALAPPDATA%/mdc-engine-build` (same versions as CI). `cmake -S combo-engine -B <that>/ce-build -A x64 -DOCGCORE_DIR=<that>/ce-src/ygopro-core
  -DDEPS_DIR=<that>/ce-deps -DMIMALLOC_DIR=<that>/ce-src/mimalloc` then `cmake --build <that>/ce-build --config Release`.
  Test data: the app's `%APPDATA%/Omni HERO Overlay/combo-data/` (cards.cdb, CardScripts.zip). Bench prints GY/banished.
- **Engine bench**: `combo-engine/bench/bench.py <engine> <cards.cdb> <scripts.zip> <hand codes> <secs> <maxActions> [threads] [mode] [targets]`
  with `DECK_JSON=combo-engine/bench/omni.json` (Omni HERO) or `elfnote.json`; `ZONES=0/1` forces zone mode.
  Reference results (Stratos + Faris, Omni list): ends on Sunrise ×2 + DPE + Favorite Contact set (FC -> Shining Neos
  Wingman live: Neos + Infernal Rage in GY/banished). Elfnote Lucina (120s, after guide tuning): Crystal Wing + Dawn
  Dragster + Accel Synchro Stardust (center), Welcome Home + Rhapsodia set, Strelitzia in GY (15.04). The guide board
  (Baronne + Crystal Wing, Rhapsodia set, Strelitzia in GY) scores 14.47 and is reached when Baronne is a goal; see
  `bench/refs/elfnote.json`.
- **GitHub deep test**: edit `combo-engine/bench/run.json` and push (this session's GitHub access can't start runs by API).
  Each job posts its top boards as annotations, readable without logging in: list jobs with
  `curl -s https://api.github.com/repos/gabegriffin16-commits/ygo-md-companion/actions/runs/<run>/jobs`, then
  `.../check-runs/<job id>/annotations`. Matrix entries can set `deck` (e.g. `combo-engine/bench/elfnote.json`).
- **Engine test button**: post a plan with `tools/benchtool.py post plan.json` (env `BENCH_USER`/`BENCH_PASS`, the Test
  account; ask the owner), they press Settings > Engine test > Run in the app, then `tools/benchtool.py results`.
  Plan: `{"name":..., "spec":{"engine":"app"|"test", "cases":[{"name","deck","extra","hand","timeMs","maxActions","targets"}]}}`.
- **Page tests**: Playwright against a static server of the repo. Init scripts in `tools/test/`: `supamock.js` (Supabase),
  `appmock.js` (overlayApp), `combomock.js` + `engine-bridge.py` (Find combos), `genmock.js` + `gen-bridge.js` (Generate lines).
  `electron-harness.js` loads `app/main.js` in Node with a fake electron for IPC tests.
- Before pushing UI: screenshot full view and compact, check pixel alignment, check `pageerror`s.

## Release

Bump `app/package.json` version → push → Actions builds `MasterDuelCompanion.exe`, publishes `vX.Y.Z`, and refreshes the
`engine-test` pre-release with the newest helper. Commit messages end with the Co-Authored-By line used in history.

## Open items / ideas

- Search stopping: a full search can't finish on long combos. Add (1) stop when the best board hasn't changed for
  a good share of the time and report "stable"; (2) merge positions that differ only in ways that can't matter (GY order,
  zone of non-zone cards) so searches go faster and short combos can truly finish; (3) show complete vs stable separately.
- Reference suite: add 2-3 guide-rich decks next (MD tier list 2026-10-06: Tier 1 Dracotail, Elfnote Engine, Branded;
  pick different styles), each as a decklist JSON + `bench/refs/<deck>.json` with guide boards and sources, then tune.
  Elfnote: the guide board (Baronne) is 0.07 below the engine pick; check whether Accel Synchro's 1.5 is fair.
- The Elfnote deck's generated lines (shared deck "ydk-decklist") predate v1.18.0 and should be regenerated.
- First engine-test-button run: the owner needs to run the bench tables SQL (bottom of `supabase/schema.sql`) and update to 1.19.0.
