#pragma once

// stalker-online loot system (doc/design/loot-system.md).
//
// Weapons, suits, helmets, artefacts and detectors roll a rarity and named
// affixes the first time they come online. The roll is stored in the item's
// server entity (CSE_ALifeInventoryItem::m_rpg_rarity / m_rpg_affixes), so it
// is saved and survives the item going offline. Every time the item spawns,
// the affixes are applied to its stats (CInventoryItem::loot_apply_stat) and
// its name, description and price are rebuilt.
//
// All numbers come from gamedata/configs/rpg/loot.ltx.

#include "ItemLootStats.h"

class CInventoryItem;
class CSE_Abstract;

namespace loot
{
enum ERarity : u8
{
    eCommon,
    eUncommon,
    eRare,
    eEpic,
    eLegendary,
    eRarityCount
};

// Called from CInventoryItemObject::net_Spawn, after upgrades are installed.
void OnItemSpawn(CInventoryItem& item, CSE_Abstract* entity);

// Force the rarity of the next roll of the item with this object id (rpg_spawn).
void ForceNextRarity(u16 object_id, ERarity rarity);

pcstr RarityName(u8 rarity);
u32 RarityColor(u8 rarity);
} // namespace loot

void RegisterLootConsoleCommands();
