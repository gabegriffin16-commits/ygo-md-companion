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
  (materials, cards to send) are capped at half the beam width per pass (4, 12, 36...): several 15-way picks in one
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
  optimization (+4.5%). Profile (`MDC_PROF=1`): ~88% of search time is creating/resetting duels and replaying lines
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
  cards then our most valuable (`MDC_NOPICKRANK` to compare). Timed 20s: Dracotail +0.5, Branded +1.0, Kewl Tune same. `mode:"adaptive"` (beam on all threads + idle-priority depth-first workers) exists but
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
  Next: conditions an effect needs (script `cond`), Quick Effect vs ignition where the text doesn't say.
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
