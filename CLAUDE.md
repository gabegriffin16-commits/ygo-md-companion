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

## How to work on the engine (lessons, Oct 2026)

- Order: build the yardstick, profile, use the source of truth (core/script data before text heuristics), scan the
  whole card database, then review decks one by one, then build big features (with a pass/fail test agreed first).
- Yardsticks: `bench/tune.py` (fixed replay budget, reproducible) for search; `bench/evaldiff.py <old> <new> <cdb> <zip>`
  (every card's reading, two builds) for any text/script rule, then read every changed card; `bench/review.py` per deck.
  Timed runs swing ±0.7 per hand: never judge from one. Run timed benchmarks alone (scans compete for the CPU).
- When adding a cap or filter to the search, log what it keeps on one real example (the End Phase "no chain" bug).
- Keep reusable tools in `combo-engine/bench/`; session scratch folders get wiped.

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
| `deck.json`, `rules.json` | Omni HERO play data: hand-written lines, play rules, checkpoints. Used by decks with `base:"hero"` (the owner's Omni HERO deck and copies), the tutorial deck and `omni.json`. |
| `cards.json` | Card database for the page (from YGOPRODeck; `update_card_db.py`). |
| `app/` | Electron overlay app (`main.js`, `preload.js`, screen reader `reader*.js`, `handreader.js`, `hudreader.js`). |
| `combo-engine/` | The combo finder helper (C++, AGPL-3.0, separate program). See its README for the protocol. |
| `supabase/schema.sql` | Accounts, decks, engine-test tables + row-level security. The owner runs it in the Supabase SQL editor. |
| `tools/` | Test mocks/bridges and `benchtool.py` (see Testing). |
| `.github/workflows/build-app.yml` | Builds the Windows app + helper, smoke-tests the helper, publishes releases. |
| `.github/workflows/engine-bench.yml` | Deep-search test on GitHub runners; triggered by changing `combo-engine/bench/run.json`. |

Site: GitHub Pages from `main`. App: portable exe from GitHub Releases; it checks `releases/latest` and offers updates.

### Page modules (in index.html)
- `MDC`: decks in localStorage, scoped per account (`MDC.scope(uid, isAdmin)`; `MDC.isAdmin()` is the admin flag; `MDC.key(name)` = per-account localStorage key).
  No built-in deck any more: Omni HERO is a regular shared deck on the owner's account (`omni:true`, `base:"hero"`), made
  once from deck.json by `ensureOmni()` at boot (moves old device-saved `mdc-herolines` onto it; stored pilot "hero" maps to it).
  `PLAYRULES` (was HEROMODE) = the deck has hand-written play rules (base "hero"). `WRITTEN` = PLAYRULES and no generated
  lines: once Omni HERO has `deck.gen` it shows generated Lines / Checkpoints / Counter Guide / best line like every other
  deck (owner's choice); its play-rule trackers (stop box, once-per-turns, locks) stay.
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
- Back / forward: `NAV` keeps the app's own history (main tab, Users page via `USERS_NAV`, builder deck + view);
  `NAV.rec()` is called on tab switch, Users paint, builder open/close/view. Buttons first in the app bar (`#ovLeft`) or
  the website header; Alt+Left/Right and mouse back/forward work too. Leaving the builder with unsaved changes goes
  through `root._leave(go)` (Save / Don't save / Keep editing); Save is gold only while there are unsaved changes.
- Lines outside the Lines tab: `deckLinesEl(deck)` renders a deck's generated + saved lines read-only; used by the builder's
  Lines view (`.bldlines`, data-v "lines") and the Users deck view's Cards | Lines toggle (shown when the deck has lines).
- Generated lines: `genBox` (builder > Analysis and builder > Lines, both live on `GEN.paints`: "Set end-board goals" fold + Generate + progress), `paintGen` (Lines / Checkpoints / Counter Guide
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
  optional activations are decision points (Quick-Effect decks combo inside chains). Card picks inside one action
  (materials, cards to send) are capped at half the beam width per pass (at least 2: 4, 12, 36...; `MDC_PICKMIN`, 2 beat 4 on `tune.py SET=guideless` at 5k/15k/30k replays: 90.38->90.48, 92.46->94.69, 96.18->96.79, tune set unchanged): several 15-way picks in one
  action used to eat a whole search (one beam level took 81s of 120). One-at-a-time picks are tried in one order per
  set. The line to the best board so far is always kept in the beam. Debug env: `MDC_BEAMLOG=1` (per-level timing),
  `MDC_NOPICKCAP` / `MDC_PICKMIN` / `MDC_NOPICKORDER` / `MDC_NOELITE` (A/B switches), `MDC_TRACE=<labels.json>` (rank
  of a known line's state at each beam level, and the top line; get labels from a search with `"labels": true`).
  A monster that banished itself "until the End Phase" (Ecclesia and the Dark Dragon) scores as on the field.
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
  Rules tuned against guide boards: summoning itself is no stop; a revival 0.5 (+ what it brings back, chains included);
  an opponent's-turn summon from the hand / Deck / Extra Deck is worth the best monster it can actually bring (`bring*`
  in CardEval, judged in score_of: Accel Synchro -> "Stardust Dragon"; Remix / Kewl Tune Synchro -> a Tuner Synchro
  whose Level the materials can hit exactly, Remix only with 2 non-Synchro Tuners in the GY; Ecclesia -> a "Swordsoul"
  or "Fallen of Albaz" left in the Deck/hand; each card claimed once, one use per summoner name; unknown Deck/Extra
  -> the old generic 1.5); Fallen of Albaz landing on their turn = removal 2.0 (fuses with their monster); negates: "a card or effect" +0.5, Spell/Trap-only -0.7, "would destroy" -1.5;
  attack-only locks 0.5. End hand: handtraps are stops, Traps/Quick-Plays count as set, an extender that summons itself
  from hand on their turn 0.8, a next-turn starter (searches the Deck) 0.6, anything else 0.3.
- **Opponent's-turn simulation** (`"sim":true`; `simulate` / `sim_once` in main.cpp): re-ranks the returned boards by
  playing the opponent's turn for real. Probe hand (each card = one kind of play, none with a Quick Effect): Evil HERO
  Adusted Gold (hand effect that searches), Reinforcement of the Army (Spell that searches), Goblindbergh (Normal Summon
  + trigger that Special Summons Photon Chargeman), Chargeman's field effect, Gagaga Cowboy (Extra Deck summon), then an
  independent second wave (Pot of Greed, Photon Thrasher when their field is empty, Upstart Goblin) so a board's 3rd/4th
  interruption still gets a target. Our line is replayed from our own responses (`Found.mine` + the prompt type each
  answered: a prompt the line never saw, like a "look at your opponent's hand" option that only exists now that they
  hold cards, gets a default answer); Traps/Quick-Plays in hand get Set before the turn ends (as the text score
  assumes). A board whose line still doesn't replay keeps its text score scaled by the search's median sim/text ratio. A play counts as stopped only if tried and its result
  didn't happen (checked on the field/hand); plays that never came because an earlier one was stopped give no credit.
  Our choices on their turn: local search (start greedy, remove wasted Quick Effects all at once, then single-decision
  changes with a greedy replay of the rest; budget `MDC_SIMRUNS`, default 150 play-throughs). One Quick Effect of ours per
  opponent play; triggers (told apart by the engine's 0x7f trigger prompt) always allowed. Our own picks default to
  their cards first, then the most valuable monster. Credit: one per interruption at its card's text value; a card
  that came out during their turn (Favorite Contact's Wingman) merges with what brought it on the same play (copies
  told apart by zone + MSG_MOVE); one interruption stopping two plays adds 0.3. Final score = text score − text stops
  + simulated stops (same fall-off); interruptions that never got a chance (battle-only Sunrise, Nibiru, Stardust
  Dragon's destruction negate) keep half their text value; ones that had the chance and stopped nothing count 0.
  Not modeled yet: battle, opponent handtraps against our end board. `bench/sim.py` prints the per-board breakdown;
  `MDC_SIMLOG=1` logs every decision.
- **Zones**: only for decks whose text mentions the center zone/columns (`g_zones`); zone-aware cards go center or side
  by rule; Normal/Extra Deck summons of them branch both ways.
- **Draws** come from 12 blank stand-ins (Spiral Serpent) on top of the Deck, hidden in output (shown as code 0).
- **Handtrap backups**: `oppHand` + `prefix` (choice labels of a line) + `hitCard/hitStep` replays a line and chains
  Ash/Imperm/Veiler/Droll at that step.
- **Search output**: `complete` (every line checked), `stable` (complete, or the best board stopped improving in the
  last 40% of the time), `stats.bestAt` (seconds). Request `stable`: 0..1 enables the early stop (`stableMinMs`, default 5000).
  Bench: `STABLE=0.4`.
- **Speed**: finished duels are reset and reused (`src/reset.cpp`, ~2x); bytecode stripped; mimalloc; link-time
  optimization (+4.5%). **Saved games (Oct 2026)**: each duel's memory (its C++ objects and Lua state) lives in its own region
  (`src/arena.cpp`: a slice of reserved address space whose allocator bookkeeping is inside the slice; the region is current
  on a thread only during calls into the core, see `ArenaScope`; our own code's memory must stay outside: `script_reader`
  and reset.cpp's tables switch it off). A game in progress is saved by copying the region's used bytes and restored by
  copying them back to the same addresses (`Duel::save/load`). The beam (`sub`) and the depth-first workers (`dfs`) save at a
  branch point and restore for each alternative instead of replaying the line from the start. Proven identical single-
  threaded (tune.py 3000: 110.25 on all 11 hands with plain / regions / restores; dfs at a fixed budget: same boards on 3
  hands); 2.6-3x more branches per second in the default mode (Snake-Eye 11.8k -> 31.2k in 10s, Dracotail 15.6k -> 46.4k,
  HERO 19.9k -> 51.1k); fixed-budget wall time 2.3x shorter (beam); peak memory 482 -> 547 MB; compare.py 20s: Dracotail
  188.30 -> 200.66, Branded 161.08 -> 171.55 (guide better 4 -> 2), Kewl Tune 221.02 -> 225.33. On by default;
  `MDC_NOSNAP` = the old replays, `MDC_ARENA` = regions only, `MDC_ARENACHECK` = fill freed blocks and check on reuse,
  `MDC_ARENALOG` = report region exhaustion. Saves share pages (src/arena.cpp `mdc_snap_*`): regions are reserved with
  Windows write-watch, a save taken while an older one is live copies only the pages written since it, and a restore copies
  back only the pages written since its save (marks go to the newest save, handed down when it's removed; Linux: full
  copies). A branch writes ~250-350 of ~900 pages (not Lua's garbage collector: stopping it changed nothing), so restores
  run near memory bandwidth. Identical results again (tune.py 3000 single-threaded: 110.25; dfs: same boards on 3 hands).
  Clean machine, 10s default mode, old replays (`MDC_NOSNAP`) -> now: Snake-Eye Ash + Marshmao 12.3k -> 56.6k branches
  (4.6x, best 13.00 -> 15.25), Dracotail Walbaz + Lukias 15.9k -> 114.5k (7.2x), HERO Stratos + Faris 22.1k -> 107.1k
  (4.8x, 8.20 -> 9.87 = the Sunrise x2 reference board). (Check for leftover engine processes before timing: a hung test
  once skewed these.) End Phase deep passes now run in the background while the beam goes on (`ep_deep` / `ep_join`; `MDC_EPSYNC` =
  wait, as before): on Dracotail they took 8.4 of 10 s with only 1-4 turns each (most threads idle). Beam mode 36k -> 76k
  branches in 10 s on Walbaz + Lukias, best 10.65 -> 12.39 (guide 12.70). compare.py 20s: Dracotail 201.67 -> 205.36
  (guide better 2 -> 1), Branded 175.96 -> 175.27 (noise); tune.py 15k: std 113.89 -> 114.02, guideless 95.06 -> 93.33 (one
  Snake-Eye hand that swings 14.6-16.8 under many settings). Search order is timing-dependent with it on. `MDC_PROF` prints save/restore counts, pages copied / shared and
  write-watch time. Next: each beam state is still replayed once before it's expanded. Memory (fixed Oct 2026): page copies come from a pool
  (slabs, one shared free list, a reference count per page), and every search thread ends with `mdc_thread_done()`
  (`ThreadEnd` in main.cpp): the statically linked mimalloc missed many Windows thread exits (1,200 of 3,100 threads still
  "alive" after 10 searches), so their memory was never reclaimed; private bytes grew ~250 MB a search (14 GB once, the
  engine then crashed). Now flat (~490 MB private over 20 searches; 700 MB over 40 handtrap-backup searches).
  `bench/memloop.py` checks it; `MDC_PROF` prints live saves, page pool, duels pooled, regions committed. Profile (`MDC_PROF=1`): ~88% of search time is creating/resetting duels and replaying lines
  (no way to copy a game state), scoring ~0.1%. Beam passes reuse the previous pass's expansions (~20% per pass;
  `MDC_NOREUSE` to compare). Inside one action, the same choices in another order reaching the same board + prompt are
  cut mid-action (`MDC_NOMIDDEDUP`). End Phase: on decks with several End Phase effects (Dracotail / Branded) trying
  every yes/no, trigger order and pick from every state was ~97% of all replays (`MDC_BRANCHLOG=1` prints where
  expansions branch); now yes/no = yes until width 216, trigger order = first until 72 (Quick Effect windows: every
  option, Branded needs them), card picks ordered by the game's hint (add/Set: most valuable; costs: least) and 2 until
  72 (`MDC_NOEPCAP`, `MDC_NOEPCHAIN`, `MDC_NOEPPICK`). Each End Phase is first played on best guesses only (use the effects, ranked picks); turns whose board lands within 1.5 of the best so far get every End Phase choice after that beam level (`ep_deep`; `MDC_NOEPGREEDY`, `MDC_EPMARGIN`, `MDC_EPLOG`). Fixed with it (Oct 2026): the End Phase sort missed the "no chain" label, so below width 72 optional End Phase triggers were declined. Fixed budget (tune.py, 30000): 115.21 -> 118.68 (Dracotail Branded Fusion 9.93 -> 13.00, nothing lower); compare.py 20s: Dracotail +8.4 total, Kewl Tune same, Branded within noise (the depth-first workers find a hand's 13.17 board 1 run in 3). Walbaz + Lukias still ~10.5-11.3 at 60s (guide ~16). Timed (20s) guide hands: Dracotail +5.1 total, Branded +3.5,
  Kewl Tune same; fixed-budget tune set and HERO/Elfnote unchanged. Capped card picks are tried best-guess first (then
  the fixed scattered order): for materials / costs (the game's hint), cards that act when used as material (`matTrig`:
  a Dracotail sent as Fusion material Sets a Trap), then Deck > GY > hand > field, cheaper first; for picks we take, their
  cards then our most valuable (`MDC_NOPICKRANK` to compare). Timed 20s: Dracotail +0.5, Branded +1.0, Kewl Tune same. Mid-chain states (a chain window with our triggers waiting, e.g. a Dracotail sent as material about to Set a
  Trap) were ranked against finished ones unfairly: each waiting activation now adds 0.5 to the state's promise
  (`MDC_WWIN`), and the Normal Summon bonus counts when the line hasn't used it (the game doesn't offer it mid-chain).
  Found by tracing Dracotail "Branded Fusion 1cc" (the guide's board is reachable, 10.93, but was cut at beam level 3).
  tune.py before -> after: std 15k 109.45 -> 114.02, 30k 119.10 -> 119.34; guideless 15k 91.63 -> 94.18, 30k 96.13 -> 97.49
  (1.0 gained a bit more but dipped once). compare.py 20s, before -> after: Dracotail total 181.85 -> 188.30 (guide better 3 -> 2: Branded Fusion 1cc
  now found), Branded 147.51 -> 161.08 (5 -> 4). Left: mostly guide boards with guessed placements or missing cards. `mode:"adaptive"` (beam on all threads + idle-priority depth-first workers) exists but
  isn't used: on 6 physical cores it slows HERO's beam. `maxReplays` = work budget instead of the clock and `growth`
  = beam widening factor (`bench/tune.py` tunes the search on a fixed budget, nearly reproducible; GitHub runs it per
  configuration in parallel via `engine-tune.yml` when `bench/tune.json` changes; runner and PC totals match, e.g.
  118.74 vs 118.70). Sweep at 30000 replays/hand over 11 hands / 5 decks: start width 8/16/24, growth 2/3/4 and each
  ranking weight halved/doubled all land at 117.1-119.0 vs the default 118.7 (noise ~1): defaults kept.
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
  search actually reaches. Master Duel Meta guides: `bench/mdm.py deck|refs` imports a top decklist and a guide's
  combos (end-board locations read from the guide's steps; missing cards and guessed placements flagged; set
  `MDM_CACHE=<folder>` to reuse fetched combos), `bench/compare.py <engine> <cdb> <zip> refs/<deck>.json <secs>` runs
  the search from each guide hand and prints guide vs engine (env THREADS, ACTS, WHY).
- **Local build on the owner's PC**: VS 2022 Build Tools + CMake are installed; deps live in
  `%LOCALAPPDATA%/mdc-engine-build` (same versions as CI). `cmake -S combo-engine -B <that>/ce-build -A x64 -DOCGCORE_DIR=<that>/ce-src/ygopro-core
  -DDEPS_DIR=<that>/ce-deps -DMIMALLOC_DIR=<that>/ce-src/mimalloc` then `cmake --build <that>/ce-build --config Release`.
  Test data: the app's `%APPDATA%/Master Duel Companion/combo-data/` (`Omni HERO Overlay` before 1.25; the app moves it on first start) (cards.cdb, CardScripts.zip). Bench prints GY/banished.
- **Engine bench**: `combo-engine/bench/bench.py <engine> <cards.cdb> <scripts.zip> <hand codes> <secs> <maxActions> [threads] [mode] [targets]`
  with `DECK_JSON=combo-engine/bench/omni.json` (Omni HERO) or `elfnote.json`; `ZONES=0/1` forces zone mode.
  Reference results (Stratos + Faris, Omni list): ends on Sunrise ×2 + DPE + Favorite Contact set (FC -> Shining Neos
  Wingman live: Neos + Infernal Rage in GY/banished). Elfnote Lucina (120s, after the summon-value rule): Crystal Wing +
  Dawn Dragster + Accel Synchro Stardust (center), Welcome Home + Rhapsodia set (15.41; before: Junora board 15.24). The guide board
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

- GUIDE CRAFTER (owner, Oct 2026): the app becomes a guide crafter on par with Master Duel Meta guides, not a lines
  generator; a UI restructure will follow (structure to be decided with the owner; the engine session prepares the guide
  data model + a proposed page structure). Engine roadmap, in order: unseen-archetype guide comparison (`bench/unseen`,
  `refs/<deck>.json`, `bench/guidetally.py`) + hand coverage; handtrap backups vs guides' "vs handtrap" lines; resilience in
  the score; play-around (safer) lines; step explanations (UI mockup to the owner first); guide data model for the UI.
  Unseen-archetype comparison (Oct 2026, 20s/hand, decks = current top list + the cards each guide uses): 78 comparable
  hands of 97 (19 handtrap/OTK lines set aside): found the guide's exact field 3, engine better by score 41, even 4, missed
  30. Misses cluster in long resource-building combos: Memento 7/7 (guide boss Mementoal Tecuhtlica - Combined Creation
  needs 5 different Mementos; reachable as a goal in 15 s but the plain search drops the line at beam level 5, rank
  3287/10221, and 120 s doesn't help), Yubel Fiendsmith 9/11 (Fiendsmith/Unchained into Desirae / Varudras / Caesar), D/D/D
  2/3; Rikka's 5 are guessed placements. Many "engine better" are partial imports (Dragonmaid guide boards = Sheou +
  Hieratic Seal only). Auto-picked boss goals (`bench/steer.py`) don't help (Memento 60.65 -> 61.41). Coverage: guide
  hands are 59% one named card, 31% two, 8% three+; generation covers every analyzer-tagged single card, starter x card
  pairs up to 60, no 3-card hands; its maxActions 16 doesn't limit anything (same boards at 40).
  Handtrap backups vs guides (`bench/vsguide.py`; hands via `bench/guidehand.py`, compare.py's slot rules): for each guide
  "into / vs / through <handtrap>" line, the engine's main line is hit at every legal step (interrupting search) and its
  backup compared with the guide's board after the hit. 14 lines: engine backup >= guide at the first legal hit 8/14,
  at the worst step 7/14. Strong: D/D/D 4/4, Phantom Knights, Kewl Tune vs Ash / Belle. Weak: Branded "Walbaz through
  Droll" (13.61 vs 8.16), Kewl Tune Rotary vs Droll, all Yubel Fiendsmith Droll / Nibiru lines (guide 11-16, engine 4-10,
  1.10 at Nibiru's worst step). MDM combos carry `vsHandTraps` (what the line is said to play through: Nibiru 29, Droll
  21, Fuwalos 11, Purulia 6, Maxx 5, Ash 4 of 105) and `condition.branches` (alternate routes) in the cached combos.
  Mulcharmy / Maxx lines are left out (the score doesn't count their draws: they need their own goal). The app's backups
  (`genBackups`) cover Ash / Imperm / Veiler / Droll only (no Nibiru, the handtrap guides plan around most), 2.5 s and 6
  actions each.
  Resilience (task 3): choosing among the engine's top lines by how they survive Droll / Nibiru barely helps
  (`bench/resil.py`: Yubel 0 of 11 hands change, the top lines share the same fragile opening; Branded 2 of 12, worst
  6.67 -> 7.09). What works: `"oppFirst": <handtrap code>` on a normal search (beam + depth-first) makes the opponent
  play that handtrap at its first legal chance, so the search finds the line built to play through it (like a guide's
  "into Droll" combo). vsguide.py FIRST=1, 14 guide lines: engine >= guide's board after the hit 10/14 (old backups:
  6/14); e.g. Yubel Samsara vs Droll 4.20 -> 14.51 (guide 11.32), D/D/D Doom Queen vs Droll 2.05 -> 14.76 (12.81), Kewl
  Tune Rotary vs Droll 4.40 -> 11.14 (6.90). Still behind: Branded Walbaz through Droll 10.12 (13.61), Yubel Nightmare
  Throne vs Nibiru x3 12.3-12.7 (13.0-16.2): the long-combo decks.
  Long combos (task 7, Oct 2026): found and fixed a beam bug: a chain window's duplicate key had no record of what's on
  the chain, so the window after activating an effect was cut as a duplicate of an earlier window with the same board /
  hand / choices (Memento Dark Blade's Main Phase effect never resolved: beam-only search "complete" after 12 replays).
  Window keys now include the sorted labels since the last Main Phase action. tune.py 15k: std 114.02 -> 114.49,
  guideless 94.59 -> 94.30 (noise); 30k std 119.74 -> 119.31; Yubel Fiendsmith guides +3.6 total. Memento's real gap
  stays (guides 13.7-13.9, engine ~8-10.7 from 1 card): `bench/tracegoal.py` (goal line, then MDC_TRACE with the promise
  split into board / hand / GY / moves) shows the guide-like line cut at beam level 3 (rank ~520 of 650) right after
  Tatsunootoshigo fills the GY: board 0.5 and 2 moves vs the top states' 3.5 and 4. Even with all 7 guide end cards as
  goals, 60 s reaches 4 of 7. Tried: weighting GY cards the deck can use (own GY effect from the script read, or named by
  a "from your GY" add/summon) in the beam's promise (`wGyUse`, off by default): rank 524 -> 389 at 0.8; tune 15k at 0.4
  std +0.6 / guideless +1.0, at 0.8 -0.7 / +1.2; Memento / Yubel guide totals within noise. Promise weights can't lift
  a state that's behind on everything visible: the next step is lookahead (a greedy playout per candidate state), a
  bigger feature that needs a pass/fail test agreed first.
  Lookahead tried (Oct 2026), FAILED the agreed test, left off: request `lookahead` K / `laSteps` N / `laMix` (env
  MDC_LOOKAHEAD / MDC_LASTEPS / MDC_LAMIX; main.cpp `look_level`): each level, states ranked below K x width get a greedy
  playout (every choice at each decision point tried once, default answers inside actions, best next promise kept, N
  decisions or to the turn's end), rank = h + mix x (playout - h) when higher; playouts count as replays. It does what it
  was meant to on the trace: Memento Dark Blade's guide-like line goes from rank ~520 to 1-3 at level 3 and survives to
  level 9 (was cut at 3). But the boards don't follow. tune.py 15k (current 114.49 / guideless 93.89): K3 N6 112.30 /
  96.86; K2 N6 111.80 / 95.60; K3 N3 112.38 / 100.67; K3 N2 109.64 / 94.38; K3 N3 mix 0.5 113.28 / 93.08; K2 N3 mix 0.5
  113.18 / 94.49 (std always below -1.0: Elfnote 14.93 -> 12-14, Branded 14.43 -> 9.7-13.5, Dracotail Branded Fusion
  9.93 -> 8.93). compare.py 20s x2, K3 N3: Memento engine 60.68 / 62.41 -> 56.28 / 58.68 (guide 90.08, engine >= guide
  0 of 7 either way), Yubel Fiendsmith 189.11 / 190.21 -> 189.13 / 184.00 (guide 222.13). Playouts saturate (many states
  get the same greedy endpoint value) and cost replays the beam needed. Not worth more tuning without a new idea (e.g.
  playouts done on the live duel at child creation instead of a replay, or a learned value). Note: guide 1cc hands are just the named card; spell fillers
  that can be Set (Forbidden Droplet) add ~7 points of held "set" value, so don't judge Memento with them.
  Monster placement (Oct 2026): monsters go to the first free zone (`r_place`); zones only branch for center-aware
  decks. Milestone goal searches on Memento Dark Blade (Creation King, Twin Dragon, Cross-Sheep, Spright Elf, Zoroa; 3
  runs each): Zoroa is never reached with default placement, always with every free zone tried (`MDC_ALLZONES` test,
  since removed): the line needs Proxy F Magician in the right Extra Monster Zone so its arrows point at free zones for
  Artemis, then Zoroa. But trying every zone costs too much: tune 15k std 114.49 -> 97.28, Memento guides 61.7 -> 51.9,
  Yubel Fiendsmith 192.3 -> 174.9. Link-aware placement (opt-in `MDC_LINKZONES`, `zone_opts` + `link_points`: one choice
  per kind of zone when a Link is involved: Extra vs Main Monster Zone, pointed to by our Links or not, and for a Link how
  many of our monsters / free zones it points to): std 114.13, guideless 93.34, Memento guides 58.7 -> 62.6, Thunder
  Dragon 276.1 -> 284.4 (21/26), Yubel Fiendsmith 189.5 -> 175.5: off by default. Still doesn't reach Zoroa (the choice
  that matters there is mirrored on an empty board and broken later by "first free zone" for main-deck monsters).
  Smart default placement (ON, `MDC_NOSMARTZONE` = old first-free zone; one choice, no extra branching, only when a Link
  is involved): a Link goes where its arrows point at the most free zones (then our monsters); any other monster goes to a
  zone pointed to by a Link that wants it (text "points to"; "Fusion Summoned to a zone" = Fusion Monsters only: Proxy F
  Magician), else a zone no Link points to, else one pointed to by a Link that doesn't care (kept free for Extra Deck
  monsters). Memento Zoroa as a goal: 0/6 -> 6/6. tune 15k std 114.49 -> 114.29, guideless 94.30 -> 94.45; guides (20 s,
  averages): Memento 62.3 -> 62.7, Yubel Fiendsmith 192.2 -> 194.6, Thunder Dragon 276.1 -> 288.0 (21/26). A first version
  that avoided every pointed zone cost Memento ~5 (it kept monsters out of Cross-Sheep's zones). Memento's plain search
  still doesn't find Zoroa unasked: that's ranking (next: a per-deck learned position value).
  Baseline guide tally after the window-key fix (`work/u3_*.txt`, 20 s): 78 comparable hands, found 4, better 39, even 4,
  missed 31 (Memento 7/7, Yubel Fiendsmith 9/11, Thunder Dragon 6, Rikka 5 with guessed placements).
  Guide builder speed + stability (Oct 2026): starters.py now deals one shared pool of 100 hands (each counts for every
  engine card in it; top-up to N=12), early-stops single / pair / handtrap searches (`stable` 0.4), and decides 2-card
  starters by pair tests: every two engine cards that aren't 1-card starters, with non-engine filler; a pair "works" when
  it builds >= 4 using both, and the card its line opens with is the 2-card starter (the other "combos with" it). The
  old rule (opens >= 30% of its hands) swung 19-50% for Ecclesia between runs. Engine-by-usage now counts only plays
  where the card itself acts (not costs: Triple Tactics Talent sent for Diabellstar), in >= 25% of its sample hands.
  `bench/rolediff.py` compares runs. Result: Dracotail 12-13 min (was ~45), 0 of 19 roles change over 3 seeds; Snake-Eye
  13-14 min, 2 of 17 change (Magicians' Souls: same Magicians' Souls + Oak combo, the line opens with either card;
  Flamberge Dragon used in ~50% of its hands, extender vs situational).
  Guide-miss triage (Oct 2026, current engine): 24 missed of 75 comparable (was 31 of 78): Memento 6, Yubel Fiendsmith 9,
  Rikka 5, Sky Striker 2, D/D/D 1, Thunder Dragon 1. At 120 s: D/D/D Kepler found (16.26 vs 14.48), Rikka 3 of 5 even or
  better; left: Rikka Early Jasmine / Rosalina (guide's Xyz/Link tail: as goals 60 s reaches 3/6: long-combo reach),
  Sky Striker 2 (0.3-1.0 behind, guessed boards), Thunder Dragon WAlbaz + TDdark. Tool fixes: compare.py fills
  "<LIGHT/DARK>"-style slots with a monster of that Attribute (it had picked Allure of Darkness: Cartesia + Bystial is
  engine 14.21 vs guide 6.80 now), and marks hands with no legal play as not comparable (follow-ups needing an earlier
  line, K9 opponent's-turn lines, Extra Deck "starting cards"). starters.py REFINE (20 s): each main line gets a longer
  search seeded with the line found.
  Learned position value (opt-in `learn` / MDC_LEARN = weight; main.cpp pos_feats / learn_note / learn_fit): every
  position the beam or the depth-first workers reach is a sample (features: which deck cards are in hand / field / Set /
  GY / banished, open moves, Normal Summon); each line carries its positions (St::anc) and every recorded board raises
  their "best reached"; a linear model of best - board now is refit at each beam level and adds weight x its prediction
  to the rank. Agreed test (20 s guides x2, tune 15k): beam-only first version: Memento 62.7 -> 63.4, Yubel Fiendsmith
  194.6 -> 201.2 (+3.4%), std -0.97, guideless +0.96; with depth-first samples (current code): std 114.86 -> 115.61,
  guideless 96.37 -> 97.47, but Memento 59.3, Yubel 191.5 (the bookkeeping costs 2-7% of replays). FAILED (needs +5%).
  Chaining ten 20 s searches seeded with the previous best stays at 9.93 on Memento Dark Blade. Next: a guided replay of
  the guide's own steps to tell "the engine can't play this line" from "the search doesn't find it".
  Guided replay (`bench/guidereplay.py` + request "guide": the guide's acting cards in order; picks / zones / targets free;
  Field Spells may be activated anytime; guided states rank by progress): the engine plays Memento Dark Blade's guide
  line through its first 14-15 of 31 acting cards, then a needed card isn't where the guide has it. The difference is the
  picks: the guide sends Goblin + Shleepy with Tatsunootoshigo, fuses Mace + Tatsunootoshigo + Dark Blade, sends Akihiron
  with Creation King (cards that act from the GY / when destroyed); we sent Ghattic / Fracture Dance by "cheapest first".
  Fix kept (ON, MDC_NOGYPICK to compare): cards going to the GY (discard / destroy / send / materials) that act there or
  when destroyed / sent (`gyGood`, from text) rank first among capped picks. tune 15k std 114.29 (same), guideless 94.45
  -> 97.16; guides Memento 62.7 -> 63.4, Yubel Fiendsmith 194.6 -> 194.8. Next: guide the picks too, to confirm the whole
  line is playable, then widen the capped picks where they matter (sends from the Deck early in a line).
  Pick sets (found with guided picks: request "guidePicks", the cards each guide step names): multi-card picks listed
  sets in the game's card order and stopped at 80 (40 for Level sums), so only sets of the first few cards were ever
  offered (Creation King "send 3 from your Deck and/or Extra Deck": ~1100 sets; the guide's never offered). Now the
  candidates are ordered best-first before building sets (`pick_order_idx`, same ranking as capped picks; MDC_NOPICKSORT
  to compare). With it the guided replay of Memento Dark Blade follows 21 of 31 acting cards (was 15); Links that care
  what they point to (text "points to": Cross-Sheep) are placed to point at one of our monsters and a free zone: 26 of
  31 (the rest: individual picks). Measured (tune 15k; guides 20 s, 2 repeats): sorted sets: std 114.29 -> 114.78,
  guideless 97.16 -> 97.89, Memento 63.4 -> 65.4, Yubel 194.8 -> 193.4; + Cross-Sheep placement: std 114.66, guideless
  97.03, Memento 67.4, Yubel 195.9, Thunder Dragon 288.0 -> 297.2 (22/26). Memento since the start of this work: 58.7
  -> 67.4 (guide 90.1). Widening capped picks from the Deck (opt-in `deckPickX` / MDC_DECKPICKX) hurt: x2 Memento 63.9,
  Yubel 188.4; x3 64.6 / 187.4 (fixed-budget sets level): off. Next: guided replays of Yubel Fiendsmith / Rikka guides.
  Guide page data (Oct 2026, mockup only, owner reviewing; UI session builds the real Guide tab once the structure is
  agreed: a new Guide tab, Lines + Counter Guide folded into it later): `bench/starters.py <engine> <cdb> <zip> <deck.json>
  <out.json>` deals real random hands and covers every Main Deck card. Engine cards (shared archetype / named by another
  card) get a role: 1-card starter (the board it builds alone with the deck's non-engine cards scores >= 4, cards still
  in hand not counted; that line is its main line), 2-card starter (lines open with it; searched with every engine
  partner, best pair = main line), extender, situational; plus reliability (share of its hands reaching the top
  quarter's floor), upgrades, "vs <handtrap>" lines (oppFirst), and how often other cards put it on the board from the
  Deck (Dracotail Horn 64%). Non-engine cards are listed. `bench/explain.py`: step explanations from the line's own data
  (what each step produced / used -> dependencies; end-board points credited back), defining steps get reasoning,
  others one line (owner's rule). `bench/guidepage2.py` renders it all, with a plain-language score rubric and a "Why N
  points" table per board (owner asked to be able to read the score).
  Generation now stores "vs <handtrap>" lines per generated line (`L.vs`: [{by, s, f, z, b, l, st}], app/main.js genRun,
  phase "vs"): an oppFirst search (4 s, beam, seeded with the line) for each of Ash, Imperm, Veiler, Droll and Nibiru
  (`GEN_VS`), kept only if the handtrap got used (steps include an "Opp" step). Data only: the page doesn't show `vs` yet
  (Guide Crafter UI, with the owner). The improver doesn't redo `vs` lines when a line improves (to do).

- GOAL (owner): be a guide maker: best lines for custom guideless decks. Tool: `bench/audit.py <engine> <cdb> <zip> <deck|.ydk|cloud:id>`
  (card reads that look silent, short-vs-long search gaps, unused cards, simulation over/underrated). First Dracotail
  run: Faimena-type Fusions from the HAND on their turn (fusionAt AT_HAND) score a generic 1.5 instead of the Fusion they
  can make (Mirrorjade / Arthalion): fix in score_of's hand loop (fusers) + a race restriction ("Dragon or Spellcaster").
  Tool to-dos: merge credit of monsters made on their turn into the card that made them (shows as Mirrorjade
  "underrated" + Faimena "overrated"); the `score` cmd has no Deck, so bring-from-Deck cards show the generic 1.5 there.
  Also per deck: read each card's text against `eval` output by hand (the guide-maker check; done for all five decks,
  Oct 2026, HERO clean). Fixed from that review: trigger conditions no longer count as effects (Dracotail Pan, Wonder
  Driver); "banished cards"/"banishment" as a place isn't banishing (Lubellion, Horn); shuffling/returning only counts
  for their cards; face-up-Spell-only negates are narrow (Dracotail Flame 3.0 -> 1.5); "cannot target" is protection,
  not a lock (Albion the Sanctifire, Loudness War); Type- or ATK-limited locks 1.5 (Secreterion, TY-PHON); "Fusion Summon"
  wording required for Fusion cards (Branded Retribution is a negate again, Branded Opening isn't a Fusion card);
  Faimena-type hand Fusions; Super Polymerization = removal; GY effects that punish removal count from the GY (Aluber);
  Synchro/Xyz/Link-summoned effects count when they land on their turn (Junora, Black Rose); Extra Deck banish minor
  (Crackle); Kewl Tune Clip / B2B bring the Synchro they can make; Loudness War copies the best "sent as Synchro
  Material" effect in the GY (`matHurt`, `copyMat`); Albion the Sanctifire revives ("2 monsters in any GY"). Effect on
  guide agreement (20s): Kewl Tune guide-better 2 -> 1; Dracotail exact matches 8 -> 5 (old matches partly came from
  Albion's fake lock / Flame's overvalue); Branded guide boards now score higher (Retribution) and the search is
  time-limited there (best found at 19-20s of 20). HERO / Elfnote: same boards (scores shift with Wonder Driver).

- Rebrand (1.25.0): app data folder is now `%APPDATA%/Master Duel Companion` (moved from `Omni HERO Overlay` on first
  start); the page publishes `window.MDC_DECK` (OMNI_DECK kept as an alias for older apps); `MDC_SITE` env (OMNI_SITE still
  read). Local storage keys (`hbc-*`, `mdc-*`) are unchanged on purpose (renaming them would drop settings).

- Search stopping (engine part done): results now carry `stable` + `stats.bestAt`; request `stable` (fraction) stops early
  once the best hasn't improved for that share of the time (off by default: runs vary, e.g. HERO's best came at 155s of
  180 once, and a 180s Elfnote run settled on a worse board than a 120s one). Beam ties are now broken by a hash of the
  line (reproducible beam runs). Zone decks key positions by center/side of center-aware monsters only (keying every
  monster's zone blew up HERO, which counts as a zone deck). The best line is now kept across beam passes. UI wording
  for complete vs stable: done (see below). Elfnote stays noisy run to run (13.9-15.0 at 20s; same engine).
- Opponent's-turn simulation (opt-in, `"sim":true`, off in the app for now): fixes real misreads (Kewl Tune Mix 1-card:
  Remix 5.15 over RS 3.80, as the guide; HERO: Destroyer Phoenix + Favorite Contact ranks first, FC credited only when
  its Fusion actually lands; Elfnote: Baronne/Crystal Wing/Rhapsodia/Tinia each credited on a different play). Since
  then: the opponent chains Poison of the Old Man to its own Pot of Greed (Zalen now negates it), attacks once in
  battle (Sunrise / Destroyer Phoenix get credit), and replays match our choices by label (End Phase options come in
  another order once Traps are Set; extra chain windows are passed): replay failures 9% -> ~1%. Still not ahead on
  "#1 board holds the guide's field" (30s per hand): text 12 vs sim 10 of 59 (Dracotail 7/8, Branded 2/0, Kewl Tune
  3/2), so it stays OFF in the app (owner's rule: turn it on only if it wins). Where it loses: Branded "nadir" ranks a
  lone Fallen of the White Dragon (6.79) over Fallen + Dogmatika + Quem + Cartesia (text 9.42): check with sim.py +
  MDC_SIMLOG whether Cartesia's opponent's-turn Fusion is found and credited (the made monster may be blocked by the
  one-Quick-Effect-per-play rule in the same play and never get a later target); Kewl Tune "Overtake 1cc" prefers
  Track Maker + Loudness War over the guide's Zalen + Loudness War. Traced "nadir": the sim was right by the rules
  (Cartesia can only make Lubellion / Dogma / Quaeritis from that board, none stops a play; Mercourier's condition,
  a Fusion that mentions Albaz, isn't met), the guide field just isn't the stronger board, so "holds the guide's field"
  undercounts the sim. Since then: Gagaga Cowboy's effect is a probe play (Extra Deck monster effects, what
  Rindbrumm-style negates answer; stops read from the engine's chain-negated messages), and never-offered
  interruptions count 0 unless their interruption waits for something the probes never do (destruction, targeting,
  banishing, GY revival, 5+ summons, Standby/Draw Phase, damage step). Latest A/B: text 13 vs sim 8. The text model
  also checks "while / if you control a ... monster" conditions on interruptions now (CardEval.needs: Mercourier,
  Rhapsodia), 30% when unmet. Yardstick now (owner chose "c"): `{"cmd":"simboard", deck, extra, field, zones, backrow,
  hand, gy, banished, targets}` plays the opponent's turn against any board set straight onto the field (monsters face-up,
  Traps/Quick-Plays Set), so compare.py SIM=1 scores the guide's own board the same way and reports which judge (text
  or sim) rates the guide board at/near the top of guide + engine boards. First run: text 15 vs sim 15 of 59 (Dracotail
  6/2, Branded 5/5, Kewl Tune 4/8); engine matches or beats the guide by sim on 47/59. Dracotail traced: our picks used
  "most valuable" for everything, feeding the best monsters into Quick Fusions; picks now follow the game's hint (what we
  summon / add: most valuable; materials, costs, Tributes, discards: least valuable; stop early when giving cards up),
  and the decision budget is 150 play-throughs (48 missed The Fallen & The Virtuous on the guide board). Still open:
  material choice keeps eating Faimena (a hand interruption of its own) as Fusion material. After those fixes: judge
  agreement text 18 vs sim 18 of 59 (Dracotail 8/3, Branded 6/8, Kewl Tune 4/7); engine matches or beats the guide by
  sim on 45/59. So: sim agrees with guides more on Branded / Kewl Tune, less on Dracotail (Quick-Fusion-on-their-turn
  decks: material / Fusion choices). Then added: material picks keep cards that can still act on their own (spent this
  turn: cheap; Fusion-on-their-turn / hand extenders / revivers: dear), and a breaker-first scenario (scenario 1:
  Dark Ruler No More if we have a face-up monster, Harpie's Feather Duster if we have backrow, Lightning Storm on the side
  it hits harder, then the normal plays; a breaker counts as stopped only if the engine reports it negated; the rules
  engine enforces DRNM's "no monster effects in response"). Score = text - text stops + average of both scenarios'
  stops. Judge agreement after that: text 20 vs sim 15 of 59 (Dracotail 9/3, Branded 7/8, Kewl Tune 4/4): breakers
  pull monster-negate guide boards (Kewl Tune) down, and Dracotail's Quick-Fusion boards still under-credit. Conclusion
  so far: the sim doesn't agree with guide boards better than the text model, so it stays an opt-in diagnostic
  (sim.py / compare.py SIM=1) rather than the app's ranking. Differences are a few hands of 59, and some guide boards
  are imported with what's made on the opponent's turn, so treat the tallies as rough. Owner agreed: not in the app;
  use it to find text-model misreads. `bench/diag.py <engine> <cdb> <zip> <secs> refs/*.json` tallies per card
  "overrated" (text counts it, it never stopped anything in play) and "underrated". First pass fixed (1.22.0): GY costs
  and "you control, or N in your GY" alternatives (Kewl Tune RS, Branded Retribution), GY-only banishing isn't a stop
  (Dracotail Sting), Extra-Deck-monster-only negates count like Spell/Trap-only (Rindbrumm), "opponent's turn" isn't
  "their card" (Rindbrumm's GY revival had scored as removal). Note: the judge tally swings 15-20 between runs with the
  same model (30s searches vary), too noisy to grade single rules; judge each fix by the card text + the regression set.
  Remaining diag leads: Zalen / Gulamel need our own chain (sim limit), Albion the Sanctifire (still a generic 1.5: it
  summons from any GY to each field), Kewl Tune Loudness War (copies a GY effect: text 0).
- Script-based reading (item 3a, owner approved Oct 2026), in progress. Done: `{"cmd":"fx","cards":[..]}` loads cards in a duel
  and lists every effect their scripts registered, read from the core (`mdc_card_effects` in reset.cpp: type, event code,
  categories, range, count limit; helper-built effects included). `fx_check` (main.cpp, once per card, at search / score /
  simboard / eval): a place (field / set / hand / GY) the text credits with an interruption but where the script has no
  effect that could act on the opponent's turn is a misread and its value is dropped (`eval` lists it as `noScript`;
  `MDC_NOFXCHECK` to compare). 2500 random cards: 6 dropped, all real (Necro Gardna, Gem-Knight Hollowcore, Tenyi
  Shthana...); 0 on the five test decks. Found with it: "this card from your hand or (face-up) field" also works from the
  field (Tri-Brigade Mercourier, Ghost Ogre). tune.py 118.68 -> 120.20. `bench/fxdiag.py` = the report (script-only
  negates too). Limits: categories don't say whose cards a removal hits, and modal cards (The Fallen & The Virtuous) set
  categories only on activation, so the text still decides *what* an effect is worth; the script decides *where/when*.
  Script-only negates over 2500 random cards found: old "Graveyard" wording wasn't read as "GY" (now normalized in
  effects()), "this card from your hand or GY" (Rikka Princess) counts from both; also attack-only negates are 0.5 (Necro
  Gardna, Magic Cylinder) and milling their Deck / their LP isn't removal (Light of Destruction, Cubic Karma).
  Wide check helpers: random 2500-card samples through `eval` before/after (19 cards changed, all checked by hand).
  Use limits (fx_check): a 2nd interruption in one place (text: +60%, `CardEval.second`) is dropped when the script says it's
  one use there: a Trap / Quick-Play activation (every mode of "activate 1 of these effects" is one activation), a
  once-per-turn effect, or effects sharing a count code (`eval` lists `oneUse`; `MDC_EVALSECOND=1` shows `second`). All
  14121 cards: 228 had a 2nd interruption, 146 now count once (single-activation Traps/Spells), 82 keep both (Continuous
  Traps with two effects, Re-qliate). Test decks: only Favorite Contact's set value 2.4 -> 1.5 (its real value is the
  Fusion-on-their-turn rule; HERO boards unchanged). Whole-database check (`eval` on every card): 19 places dropped by
  the script check, all real.
  Also: "(this is a Quick Effect)" (older wording) counts as a Quick Effect; "Special Summon it, but its effects are
  negated" is a drawback on our summon, not a negate (`negates()` in evaluate.h; 36 cards of 14121, e.g. Elder Entity
  Norden, Synchro Call; Forbidden Chalice keeps its negate). Whole-database before/after diffs of `eval` are the check for
  every text rule now (scratchpad alldiff-style: eval all 14121 cards with two builds, read every changed card).
  `negates()` also: "negate its effects" / "its effects are negated" right after the effect Special Summons or takes control
  of its own pick is a drawback (a following sentence "Its effects are negated." is merged into its effect), and "cannot be
  negated" isn't a negate (101 cards changed of 14121, reviewed; Ash / Veiler / Imperm / Solemn / Baronne / Chalice
  unchanged). `bench/negscan.py` = whole-database scan for text negates the scripts don't back.
  Guideless review (Oct 2026): `bench/guideless/*.json` = 8 current Master Duel Meta top lists the engine was never tuned
  on (Snake-Eye, Yubel, Ryzeal, Maliss (its Maliss monsters didn't map to the cdb), Tenpai Dragon, Labrynth, Memento,
  Fire King); every card read by hand against `eval`. Fixed (each as a general rule, checked with a whole-database diff):
  "the activated effect becomes \"...\"" = negate (Phantom of Yubel, Samsara D Lotus, Number 75, Dark World
  Brainwashing); "if a monster is Special Summoned to your opponent's field" / "by your Normal Trap effect" are their-turn
  triggers (Promethean Princess, Mikanko, Lovely Labrynth's destroy); "If this card is Normal or Special Summoned" effects
  count when it lands on their turn (Cooky Yummy revived by a Yummy Synchro); "send all ... from the field to the GY" =
  wipe (AA-ZEUS); "place ... on the top/bottom of the Deck" = removal (Time Thief Redoer); Link/Synchro/Xyz/Fusion using
  their monster as material = removal (Unchained Soul of Rage, W:P Fancy Ball); "3 or more attacks declared" effects never
  happen on their turn (Sangenpai); Battle Phase / Damage Step-only locks 0.5 (Mementomictlan, Ancient Gear, Utopia the
  Lightning). Known gaps left: Fire King High Avatar Garunix (revives in their Standby Phase and wipes), quoted-name
  conditions ("if you control \"Mementoal Tecuhtlica\"") are `needs` now (26 cards; a named Spell counts from the
  Spell & Trap Zone: Toon World, Macro Cosmos).
  Audits of the guideless decks (audit.py, Snake-Eye / Yubel / Labrynth): Snake-Eye's simulation showed boards winning
  through opponent's-turn Synchro/Link Summons; "Synchro/Xyz/Link Summon using monsters you control" (Yummy★Snatchy,
  Formula Synchron, T.G. Wonder Magician: 20 cards) is now a bring valued by what the field can make, and Synchros that
  "treat 1 Link-1 monster as a Level 1 Tuner" let a Link-1 count as Level 1 in the reach check. Labrynth single cards
  search completely and score low (it plays on their turn with Traps); Yubel fine.
  Lines read step by step with `bench/line.py` (no MDM guides exist for Snake-Eye / Yubel / Labrynth): Snake-Eye (Ash:
  Poplar, Sinful Spoils, Flamberge, Yummy Synchros into Little Knight) and Yubel (2x Phantom of Yubel + Samsara D Lotus)
  read like real play. Fixed from them: "cannot be destroyed by your opponent's card effects" is protection, not removal
  (43 cards: Almiraj, Hedge Guard); "this card that was Special Summoned this turn" effects are ours-only (W:P Fancy Ball).
  Labrynth links off Lady / Lovely into Unchained Soul of Rage; checked before building anything: both judges prefer that
  board over Lady + Lovely with the same Traps (text 14.70 vs 13.06, simboard 7.86 vs 6.13), so no synergy rule was added.
  Conditions: `bench/condscan.py` groups every interruption's condition (text before the colon) by pattern. Most are
  timing ("when your opponent activates"), already handled; board-state ones are few. Added `needs` kinds 3 / 4 / 5: "if
  you control no (other) monsters" (Sky Striker Mechas), "N or more cards in your hand" (Dark World Brainwashing), "no
  cards in your hand" (Infernity): 21 cards, 30% when unmet. Conditions about the opponent's board ("if your opponent
  controls more monsters") stay unchecked (unknown on our turn).
- Card scripts as the source of truth (item "3a", deferred with evidence): `bench/scriptdiag.py` reads scripts
  statically and compares with the text model. On the five test decks the text model now agrees almost everywhere; the
  static read is too noisy to drive scoring (effects built by helper functions, e.g. Fusion.CreateSummonEff, are
  invisible; whose summon a trigger reacts to is in Lua conditions). The right way, when needed: after a card's
  initial_effect runs in a duel, read its registered effects from the core (type, code, category, range, count limit;
  helpers included) via reset.cpp-style access to the core's card structures. Do it when feedback or a new deck shows
  text misreads.
- "This line is wrong" feedback: DONE (UI session). A flag button on each Duel tab / Draw simulator combo finder result
  (next to Save to Lines) and on each generated line (Lines tab and the Duel tab's best line) opens a short note and
  inserts into `line_feedback`: `deck` "<name> (<id>)", `hand` codes, `note`, `suggested` = {source ("Duel tab combo
  finder" / "Draw simulator combo finder" / "Generated line"), steps (as shown), field, zones, backrow, hand, gy,
  banished, score, complete, goal, version (app version or "web"), raw (engine steps / the generated line)}.
  Signed-in only (`CLOUD.sendFeedback`). Read with `tools/feedback.py`.
- Search result wording (owner approved complete-vs-stable wording; adjusted): "every line checked" when complete,
  otherwise "from N lines tried (best found Xs in). Searching again can find a better one." The engine's `stable` turned
  out unreliable at short budgets (HERO Stratos + Faris is "stable" at 5-20 s on 8.93, 60 s finds 10.59), so the UI
  doesn't claim "likely the best".
- Search memory + generation (1.23.0 / 1.24.0): `app/main.js` keeps the best line per deck + goals + hand in userData
  `combo-seeds.json` (newest 3000). Every combo finder / generation search sends it as the engine's `seeds` (choice
  labels replayed first, then played on through the End Phase to the opponent's Main Phase; results can't get worse)
  and saves anything better. Combo finder: a hand's first search is beam-only (`mode:"beam"`), later ones the default
  half-and-half. Generation: single cards 4 s, two-card hands up to 20 s beam-first (they share ~10 min), seeded with
  memory + their cards' single-card lines + the 10 best lines of other hands sharing a card; then the 10 best hands
  30 s more in the default mode; then backups. Then a background improver (`improverStart`): when idle (no search for
  30 s, no generation / engine test), two rounds over the lines, 30 s each, half the threads, below-normal priority;
  improved lines get new backups and the page imports them quietly (`state:"improved"`, file `quiet:true`).
  Generation uses cores-1 threads (max 15) at below-normal priority; the combo finder keeps min(8, cores-1).
  `tools/test/genquality.js` measures generation vs a 60 s-per-hand reference through the real app code (HERO set:
  98.2% first generation, 99.0% after 8 idle minutes).
- PINNED (owner): guide "vs <handtrap>" lines (vs Ash / Imperm / Veiler / Droll) are the reference for the app's handtrap
  backup lines, not for normal lines: compare them with the engine's interrupting search (oppHand + hitCard at the step
  the guide got hit, found from the guide's steps), and use MDM's per-combo vsHandTraps list to check that engine lines
  survive what the guide claims. "vs Fuwalos / Purulia" (Mulcharmy) need a new goal (fewest summons / adds); OTK lines
  only matter for a future going-second mode. Keep normal-line comparisons on true end-board guides.
  When it goes on: line generation asks for top 1,
  so it needs top 8 + sim there (app/main.js genRun), the Duel tab search already asks for 12.
- Reference suite (`bench/mdm.py`, `bench/compare.py`, decklists + `bench/refs/`), 45s per hand: Dracotail engine
  matches or beats the guide on 13 of 18 hands, Branded on 13 of 15 (the 2 Branded misses and 2 of the 5 Dracotail ones
  need cards the decklist doesn't run). Real gaps left, all Dracotail and all search reach (the guide board already
  scores higher): Branded Fusion 1cc (14.30 vs 12.60), Walbaz (SS) + Lukias (16.70 vs 13.30), Walbaz (SS) + Mululu
  (15.55 vs 13.72); the guide ends with 3 Dracotail Traps set where the engine gets 1-2. Cause found (Oct 2026): the
  End Phase ate ~97% of replays, so in 120s the beam never got past width 8; after the End Phase cap + mid-action
  duplicate cut, Walbaz + Lukias reaches width 72 in 60s and 8.60 -> 11.85 at 20s (guide 16.70 still ahead). Older notes:
  more time didn't help (best found at 1s of 180s), and beam-only / DFS-only modes didn't either. Traced (`MDC_TRACE`): the beam's top line
  IS the guide's opening (SS Walbaz, NS Mululu, Ecclesia and the Dark Dragon, Cartesia); what's missed is the long
  tail: Mululu fuses Lukias + Faimena + 1 into Arthalion, then a 3-trigger chain (Arthalion, Lukias, Faimena) sets Flame
  + Sting and returns cards, then the End Phase sets The Fallen & The Virtuous. With those 3 Traps as goals the engine
  gets 2 of them (at 79s). Next: trace with a line that reaches that chain (get it from a goals run with more time, or
  build it with `prefix`) and check whether the 3-trigger chain is being offered/ordered at all.
  Elfnote's Accel Synchro 1.5 checked: fair (it Tributes for Stardust Dragon on their turn), left as is.
  Kewl Tune (Synchro) imported: `kewltune.json` + `refs/kewltune.json` (26 guide combos; many are "vs <handtrap>" or
  OTK lines, and some end boards include what's made on the opponent's turn, so guide scores there are rough).
  Engine matches or beats the guide on 23 of 26. Real lesson from "1 Card Mix": the guide ends on Kewl Tune Remix,
  which on their turn Tributes itself, revives a Tuner and Synchro Summons (into RS, a negate) plus adds a card; the
  engine rated Remix as a generic summon (1.5). Fixed by the summon-value rule (opponent's-turn summons worth what they
  can bring). compare.py, 20s per hand, before -> after: Kewl Tune exact guide field 3 -> 7 of 26; Dracotail 6 -> 8 of 18
  (guide better 3 -> 2); Branded guide better 7 -> 6 of 15. Note: most Kewl Tune guide boards were imported without
  their GY, so score.py undercounts Remix on those; judge by searches (real GYs).
