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

## Changing the deck

1. Edit `deck.json`: add, remove or change copies (`n`). Names must match the official English name exactly.
2. Reload the site. Any card with a red ⚑ in the decklist either isn't in `cards.json` (check the spelling) or has no play rules yet.
3. Cards without rules still show their official text, stats and rarity, and you can track them with Manual tracking. Ask Claude to add rules for them in `rules.json`.

## Card database

`.github/workflows/update-cards.yml` runs `update_card_db.py` weekly and commits `cards.json` only when the data changed. To run it now: Actions tab → Refresh card database → Run workflow.
