# Master Duel Companion App

Live site: https://gabegriffin16-commits.github.io/ygo-md-companion/

## What lives where

| File | What it is | Who edits it |
| --- | --- | --- |
| `deck.json` | The decklist: card names, copies, short names, groups, notes, Extra Deck when/why | You or Claude, whenever the deck changes |
| `rules.json` | What each card does in the Duel tab (searches, summons, costs, materials, locks) | Claude, when a new card needs play rules |
| `cards.json` | Official card data for every card (text, stats, Attribute, Master Duel rarity) | A GitHub Action, every Monday |
| `index.html` | The page itself | Rarely changes now |
| `images/` | Card art you downloaded with `download_card_images.py` | You |
| `app/` | The desktop overlay app (Electron) | Claude |

## Changing the deck

1. Edit `deck.json`: add, remove or change copies (`n`). Names must match the official English name exactly.
2. Reload the site. Any card with a red ⚑ in the decklist either isn't in `cards.json` (check the spelling) or has no play rules yet.
3. Cards without rules still show their official text, stats and rarity, and you can track them with Manual tracking. Ask Claude to add rules for them in `rules.json`.

## Card database

`.github/workflows/update-cards.yml` runs `update_card_db.py` weekly and commits `cards.json` only when the data changed. To run it now: Actions tab → Refresh card database → Run workflow.

## Desktop app

The overlay app's source is in `app/`. `.github/workflows/build-app.yml` builds it on a Windows machine whenever `app/` changes and posts `MasterDuelCompanion.exe` on the [Releases page](https://github.com/gabegriffin16-commits/ygo-md-companion/releases/latest). The release number comes from `"version"` in `app/package.json`, so a change only ships when that number goes up.

The app checks for a newer release when it starts (and every few hours). When there is one, an **Update** button shows in its top bar; clicking it downloads the new exe, restarts, and you're on the new version. The tray menu also has **Check for updates**.

## Building more decks

The deck name at the top of the decklist ("Piloting …") opens the deck menu: switch decks, **Build a new deck**, edit or delete one, **Import a .ydk file**, or **Export** the current deck as .ydk. Built decks are saved on that device only (browser/app storage), so friends each keep their own and share lists as .ydk files.

A built deck gets hand/draw reading, LP, turns, the opponent Counter Guide and manual tracking right away. Play rules, combo lines and checkpoints are written per deck (the HERO deck's live in `deck.json`, `rules.json` and `index.html`); ask Claude to add them for a deck.
