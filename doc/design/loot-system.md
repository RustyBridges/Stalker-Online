# Loot system: rolled items, rarity and affixes

Status: design for the `stalker-online` fork, agreed with the project owner.
Supersedes the level-gated trader tiers and upgrade locks of
`rpg-systems.md` (milestone 3): level now improves odds, it never locks.

## Goal

Every weapon, suit, helmet, artefact and detector is an individual item with
its own rarity and rolled modifiers, found throughout the Zone, used by the
NPCs who carry it, and bought and sold by traders.

## Decisions

| Topic | Decision |
| --- | --- |
| Rarity | 5 tiers with coloured names: Common, Uncommon, Rare, Epic, Legendary |
| Rolled item types | Weapons, suits and helmets, artefacts, detectors |
| Presentation | Named affixes build the item name ("Precise Lightweight AK-74"); the tooltip lists the stat changes |
| Loot quality | Zone danger (level / area) plus a nudge from character level |
| Upgrades | Technician upgrades still work and stack on top of rolled stats |
| NPC gear | NPCs use the rolled stats of what they carry |
| Economy | Rarity multiplies price: about 1x / 1.5x / 2.5x / 4x / 7x |
| Level locks | Removed (upgrade columns, trader tier bump) |

## Affixes

An affix is a named stat change with a value range, defined in config:

```ini
[affix_precise]            ; "Precise ..."
types       = weapon
fire_dispersion = -0.08, -0.25   ; min, max multiplier change
min_rarity  = uncommon
```

Rarity decides how many affixes an item gets and how strong the rolls are:

| Rarity | Affixes | Roll strength |
| --- | --- | --- |
| Common | 0 | - |
| Uncommon | 1 | low |
| Rare | 2 | medium |
| Epic | 3 | high |
| Legendary | 3 + a guaranteed strong one | top |

First affix set (tunable, more later):

- **Weapons:** accuracy (spread), recoil, damage, fire rate, weight,
  durability (condition loss), magazine size, reload speed.
- **Suits and helmets:** each protection type, bullet/wound armour, weight,
  carry bonus, durability.
- **Artefacts:** strength of their existing effects (positive and negative
  parts roll separately), weight.
- **Detectors:** range, weight.

## Where loot comes from

Items roll once, when they first appear in the world:

- NPC loadouts (so their corpses drop rolled gear),
- stashes / treasures,
- trader stock,
- quest and event rewards (later).

Roll quality = zone danger (configured per level, e.g. Zaton low, Jupiter
medium, Pripyat high) + a small bonus per character level, capped.

Items that existed before the loot system (old saves) count as Common.

## Traders

- Sell and buy rolled items; price = base price x rarity multiplier x the
  existing Barter / Artefact lore / trader factors.
- Restocks include rolled items using the trader's zone danger; character
  level and the trader's attitude nudge the odds.

## Configuration

All numbers live in `gamedata/configs/rpg/` (affixes, rarity table, zone
danger, price multipliers), so balancing needs a game restart, not a rebuild.

## Testing commands

- `rpg_spawn <section> [rarity]`: spawn a rolled item at your feet.
- `rpg_item_info`: print the rolls of the item in your hands.
- `rpg_reroll`: reroll the item in your hands.

## Engine work (to verify while building)

Per-item data has to persist with the item (in saves and while the item is
offline in A-Life) and reach both the player's and NPCs' copies of it. Two
candidate implementations, decided by investigation before coding:

1. **Reuse the upgrade storage.** Items already carry a saved list of
   installed upgrade sections, and the engine applies upgrade sections to
   weapon and outfit stats. Affixes could be generated upgrade-like sections
   added to that list, so persistence, stat application and stacking with
   real upgrades come for free. Risk: the technician UI validates installed
   upgrades against the item's upgrade tree.
2. **Own per-item data** in the server-side item entity
   (`CSE_ALifeInventoryItem`), written in its save state behind a version
   check, and applied by multipliers where weapon / outfit / artefact stats
   are read. More code, fewer surprises.

## Build order

1. **Item core**: per-item rarity and affixes, persistence, stat application
   for all item types, coloured names and tooltip lines, config files, test
   commands, removal of the level locks.
2. **World rolls**: rolling on spawn (NPC loadouts, stashes), zone danger,
   trader stock and prices.
3. Then the world events of `rpg-systems.md` (emissions, migrations,
   dynamic jobs), which can now hand out rolled rewards.
