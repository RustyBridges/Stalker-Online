#include "StdAfx.h"

#include "ItemLoot.h"
#include "inventory_item.h"
#include "Inventory.h"
#include "Actor.h"
#include "ActorRPG.h"
#include "Level.h"
#include "xrServer.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "xrServer_Objects_ALife_Items.h"
#include "xrAICore/Navigation/ai_object_location.h"
#include "Weapon.h"
#include "WeaponBinoculars.h"
#include "WeaponKnife.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"
#include "Artefact.h"
#include "CustomDetector.h"
#include "xrEngine/XR_IOConsole.h"
#include "xrEngine/xr_ioc_cmd.h"

namespace loot
{
namespace
{
enum EKind : u32
{
    kNone = 0,
    kWeapon = 1 << 0,
    kOutfit = 1 << 1,
    kHelmet = 1 << 2,
    kArtefact = 1 << 3,
    kDetector = 1 << 4,
};

struct StatInfo
{
    pcstr id; // name in loot.ltx
    pcstr label; // tooltip text
    bool absolute; // value is an amount (kg), not a percentage
};

constexpr StatInfo stat_info[stCount] = {
    { "weight", "Weight", false },
    { "dispersion", "Spread", false },
    { "recoil", "Recoil", false },
    { "damage", "Damage", false },
    { "fire_rate", "Fire rate", false },
    { "mag_size", "Magazine size", false },
    { "wear", "Wear per shot", false },
    { "armor", "Armour", false },
    { "burn", "Burn protection", false },
    { "shock", "Shock protection", false },
    { "chemical", "Chemical protection", false },
    { "radiation", "Radiation protection", false },
    { "psi", "Psi protection", false },
    { "carry", "Carry weight", true },
    { "artefact_positive", "Beneficial effects", false },
    { "artefact_negative", "Harmful effects", false },
    { "detect_range", "Detection range", false },
};

struct Affix
{
    shared_str id;
    shared_str name; // adjective put in front of the item name
    u32 kinds{};
    EStat stat{ stInvalid };
    float min{}, max{}; // value at roll strength 0 and 1
    u8 min_rarity{ eUncommon };
};

struct Config
{
    bool loaded{};
    shared_str rarity_names[eRarityCount];
    u32 rarity_colors[eRarityCount]{};
    float price_k[eRarityCount]{};
    u32 affix_count[eRarityCount]{};
    float strength_min[eRarityCount]{}, strength_max[eRarityCount]{};
    float weights_safe[eRarityCount]{}, weights_dangerous[eRarityCount]{};
    float level_bonus{}, level_bonus_max{};
    float default_danger{ 0.3f };
    xr_map<shared_str, float> zone_danger;
    xr_vector<Affix> affixes;
};

Config g_config;

// m_rpg_rarity values FORCED_BASE + rarity: "not rolled yet, roll with this rarity" (rpg_spawn).
constexpr u8 FORCED_BASE = 0xF0;

void ReadFloats(CInifile& ini, pcstr sect, pcstr key, float (&out)[eRarityCount])
{
    pcstr list = ini.r_string(sect, key);
    string64 item;
    for (int i = 0; i < eRarityCount; ++i)
        out[i] = i < _GetItemCount(list) ? float(atof(_GetItem(list, i, item))) : 0.f;
}

EStat ParseStat(pcstr id)
{
    for (u8 i = 0; i < stCount; ++i)
    {
        if (0 == xr_stricmp(id, stat_info[i].id))
            return EStat(i);
    }
    return stInvalid;
}

u32 ParseKinds(pcstr list)
{
    u32 kinds = 0;
    string64 item;
    for (int i = 0, n = _GetItemCount(list); i < n; ++i)
    {
        _GetItem(list, i, item);
        if (!xr_stricmp(item, "weapon"))
            kinds |= kWeapon;
        else if (!xr_stricmp(item, "outfit"))
            kinds |= kOutfit;
        else if (!xr_stricmp(item, "helmet"))
            kinds |= kHelmet;
        else if (!xr_stricmp(item, "artefact"))
            kinds |= kArtefact;
        else if (!xr_stricmp(item, "detector"))
            kinds |= kDetector;
    }
    return kinds;
}

const Config& GetConfig()
{
    if (g_config.loaded)
        return g_config;
    g_config.loaded = true;

    string_path path;
    FS.update_path(path, "$game_config$", "rpg" DELIMITER "loot.ltx");
    if (!FS.exist(path))
    {
        Msg("! loot: %s not found, items will not roll", path);
        return g_config;
    }
    CInifile ini(path);
    Config& c = g_config;

    constexpr pcstr R = "loot_rarity";
    pcstr names = ini.r_string(R, "names");
    pcstr colors = ini.r_string(R, "colors");
    string64 item;
    for (int i = 0; i < eRarityCount; ++i)
    {
        c.rarity_names[i] = _GetItem(names, i, item);
        int r = 255, g = 255, b = 255;
        _GetItem(colors, i, item, ';');
        sscanf(item, "%d,%d,%d", &r, &g, &b);
        c.rarity_colors[i] = color_argb(255, r, g, b);
    }
    ReadFloats(ini, R, "price_k", c.price_k);
    float counts[eRarityCount];
    ReadFloats(ini, R, "affix_count", counts);
    for (int i = 0; i < eRarityCount; ++i)
        c.affix_count[i] = u32(counts[i]);
    ReadFloats(ini, R, "strength_min", c.strength_min);
    ReadFloats(ini, R, "strength_max", c.strength_max);
    ReadFloats(ini, R, "weights_safe", c.weights_safe);
    ReadFloats(ini, R, "weights_dangerous", c.weights_dangerous);
    c.level_bonus = ini.r_float(R, "level_bonus");
    c.level_bonus_max = ini.r_float(R, "level_bonus_max");

    constexpr pcstr Z = "loot_zone_danger";
    for (const auto& [key, value] : ini.r_section(Z).Data)
    {
        if (key == "default")
            c.default_danger = float(atof(value.c_str()));
        else
            c.zone_danger[key] = float(atof(value.c_str()));
    }

    for (const auto& [key, unused] : ini.r_section("loot_affixes").Data)
    {
        pcstr sect = key.c_str();
        if (!ini.section_exist(sect))
        {
            Msg("! loot: affix section [%s] missing", sect);
            continue;
        }
        Affix a;
        a.id = sect;
        a.name = ini.r_string(sect, "name");
        a.kinds = ParseKinds(ini.r_string(sect, "kinds"));
        a.stat = ParseStat(ini.r_string(sect, "stat"));
        a.min = ini.r_float(sect, "min");
        a.max = ini.r_float(sect, "max");
        a.min_rarity = u8(ini.read_if_exists<u32>(sect, "min_rarity", eUncommon));
        if (a.stat == stInvalid || !a.kinds)
        {
            Msg("! loot: affix [%s] has an unknown stat or kinds", sect);
            continue;
        }
        c.affixes.push_back(a);
    }
    Msg("* loot: %u affixes loaded", u32(c.affixes.size()));
    return c;
}

const Affix* FindAffix(const shared_str& id)
{
    for (const Affix& a : GetConfig().affixes)
    {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

u32 KindOf(CInventoryItem& item)
{
    if (auto* weapon = smart_cast<CWeapon*>(&item))
    {
        if (smart_cast<CWeaponBinoculars*>(weapon) || smart_cast<CWeaponKnife*>(weapon))
            return kNone;
        return kWeapon;
    }
    if (smart_cast<CCustomOutfit*>(&item))
        return kOutfit;
    if (smart_cast<CHelmet*>(&item))
        return kHelmet;
    if (smart_cast<CArtefact*>(&item))
        return kArtefact;
    if (smart_cast<CCustomDetector*>(&item))
        return kDetector;
    return kNone;
}

float CurrentDanger()
{
    const Config& c = GetConfig();
    float danger = c.default_danger;
    if (g_pGameLevel)
    {
        const auto it = c.zone_danger.find(Level().name());
        if (it != c.zone_danger.end())
            danger = it->second;
    }
    if (g_actor)
        danger += std::min(c.level_bonus * float(g_actor->RPG().Level() - 1), c.level_bonus_max);
    return std::clamp(danger, 0.f, 1.f);
}

ERarity RollRarity(float danger)
{
    const Config& c = GetConfig();
    float weights[eRarityCount], total = 0.f;
    for (int i = 0; i < eRarityCount; ++i)
    {
        weights[i] = c.weights_safe[i] + (c.weights_dangerous[i] - c.weights_safe[i]) * danger;
        total += std::max(0.f, weights[i]);
    }
    float pick = ::Random.randF(0.f, total);
    for (int i = 0; i < eRarityCount; ++i)
    {
        pick -= std::max(0.f, weights[i]);
        if (pick <= 0.f)
            return ERarity(i);
    }
    return eCommon;
}

void RollAffixes(u32 kind, ERarity rarity, xr_vector<std::pair<shared_str, float>>& out)
{
    const Config& c = GetConfig();
    out.clear();
    xr_vector<const Affix*> pool;
    for (const Affix& a : c.affixes)
    {
        if ((a.kinds & kind) && rarity >= a.min_rarity)
            pool.push_back(&a);
    }
    for (u32 n = 0; n < c.affix_count[rarity] && !pool.empty(); ++n)
    {
        const u32 index = ::Random.randI(int(pool.size()));
        const Affix* a = pool[index];
        // One affix per stat: drop every pool entry for the same stat.
        pool.erase(std::remove_if(pool.begin(), pool.end(), [a](const Affix* x) { return x->stat == a->stat; }),
            pool.end());

        // Legendaries: the first affix is always a top roll.
        float strength = ::Random.randF(c.strength_min[rarity], c.strength_max[rarity]);
        if (rarity == eLegendary && n == 0)
            strength = std::max(strength, 0.9f);
        out.emplace_back(a->id, a->min + (a->max - a->min) * strength);
    }
}

void FormatValue(const Affix& a, float value, string128& out)
{
    if (stat_info[a.stat].absolute)
        xr_sprintf(out, "%s %+.1f kg", stat_info[a.stat].label, value);
    else
        xr_sprintf(out, "%s %+d%%", stat_info[a.stat].label, iFloor(value * 100.f + (value >= 0.f ? 0.5f : -0.5f)));
}

void ApplyPresentation(CInventoryItem& item)
{
    const Config& c = GetConfig();
    const u8 rarity = std::min<u8>(item.m_loot_rarity, eRarityCount - 1);

    // "Precise Lightweight AK-74"
    xr_string name;
    for (const auto& [id, value] : item.m_loot_affixes)
    {
        if (const Affix* a = FindAffix(id))
        {
            name += a->name.c_str();
            name += " ";
        }
    }
    name += item.NameItem();

    // Rarity line, one line per affix, then the original description.
    const u32 col = c.rarity_colors[rarity];
    string256 line;
    xr_sprintf(line, "%%c[255,%u,%u,%u]%s%%c[default]\\n", color_get_R(col), color_get_G(col), color_get_B(col),
        c.rarity_names[rarity].c_str());
    xr_string description = line;
    for (const auto& [id, value] : item.m_loot_affixes)
    {
        if (const Affix* a = FindAffix(id))
        {
            string128 stat;
            FormatValue(*a, value, stat);
            xr_sprintf(line, "%%c[255,170,200,170]%s: %s%%c[default]\\n", a->name.c_str(), stat);
            description += line;
        }
    }
    description += "\\n";
    description += item.ItemDescription().c_str();

    item.loot_set_presentation(name.c_str(), description.c_str(), c.price_k[rarity]);
}
} // namespace

pcstr RarityName(u8 rarity)
{
    const Config& c = GetConfig();
    return rarity < eRarityCount && c.rarity_names[rarity].size() ? c.rarity_names[rarity].c_str() : "?";
}

u32 RarityColor(u8 rarity)
{
    const Config& c = GetConfig();
    return rarity < eRarityCount ? c.rarity_colors[rarity] : color_argb(255, 255, 255, 255);
}


void OnItemSpawn(CInventoryItem& item, CSE_Abstract* entity)
{
    if (!IsGameTypeSingle() || !ai().get_alife() || !entity)
        return;
    auto* se_copy = smart_cast<CSE_ALifeInventoryItem*>(entity);
    if (!se_copy)
        return;

    const u32 kind = KindOf(item);
    if (kind == kNone || GetConfig().affixes.empty())
        return;

    // The spawn packet is a copy: the persistent entity lives in the A-Life registry.
    auto* se = smart_cast<CSE_ALifeInventoryItem*>(ai().alife().objects().object(entity->ID, true));
    CSE_ALifeInventoryItem* source = se ? se : se_copy;

    const u8 stored = source->m_rpg_rarity;
    if (stored == CSE_ALifeInventoryItem::RPG_UNROLLED || (stored >= FORCED_BASE && stored < FORCED_BASE + eRarityCount))
    {
        const ERarity rarity =
            stored == CSE_ALifeInventoryItem::RPG_UNROLLED ? RollRarity(CurrentDanger()) : ERarity(stored - FORCED_BASE);

        source->m_rpg_rarity = u8(rarity);
        RollAffixes(kind, rarity, source->m_rpg_affixes);
        if (source != se_copy)
        {
            se_copy->m_rpg_rarity = source->m_rpg_rarity;
            se_copy->m_rpg_affixes = source->m_rpg_affixes;
        }
    }

    item.m_loot_rarity = std::min<u8>(source->m_rpg_rarity, eRarityCount - 1);
    item.m_loot_affixes = source->m_rpg_affixes;

    for (const auto& [id, value] : item.m_loot_affixes)
    {
        if (const Affix* a = FindAffix(id))
            item.loot_apply_stat(a->stat, value);
    }
    ApplyPresentation(item);
}
} // namespace loot

// --- console commands (testing) ----------------------------------------------------------

namespace
{
bool ParseRarity(pcstr text, loot::ERarity& rarity)
{
    for (u8 i = 0; i < loot::eRarityCount; ++i)
    {
        if (0 == xr_stricmp(text, loot::RarityName(i)))
        {
            rarity = loot::ERarity(i);
            return true;
        }
    }
    return false;
}

void SpawnForActor(pcstr section, int rarity /* -1 = roll normally */)
{
    CActor* actor = g_actor;
    if (!actor || !ai().get_alife())
    {
        Msg("! loot: load or start a game first");
        return;
    }
    if (!pSettings->section_exist(section))
    {
        Msg("! loot: no item section [%s]", section);
        return;
    }

    // Same as the scripts' alife():create() for an online parent: build the
    // entity unregistered, then spawn it through the server so the client
    // object appears in the actor's inventory.
    auto* alife = const_cast<CALifeSimulator*>(ai().get_alife());
    CSE_Abstract* item = alife->spawn_item(section, actor->Position(), actor->ai_location().level_vertex_id(),
        actor->ai_location().game_vertex_id(), actor->ID(), false);
    if (!item)
        return;
    if (auto* inv = smart_cast<CSE_ALifeInventoryItem*>(item); inv && rarity >= 0)
        inv->m_rpg_rarity = u8(loot::FORCED_BASE + rarity);

    NET_Packet packet;
    packet.w_begin(M_SPAWN);
    packet.w_stringZ(section);
    item->Spawn_Write(packet, FALSE);
    alife->server().FreeID(item->ID, 0);
    F_entity_Destroy(item);

    ClientID client_id;
    client_id.set(0xffff);
    u16 message;
    packet.r_begin(message);
    alife->server().Process_spawn(packet, client_id);
    Msg("* loot: spawned %s into your inventory", section);
}

class CCC_LootSpawn : public IConsole_Command
{
public:
    CCC_LootSpawn(pcstr N) : IConsole_Command(N) {}
    void Execute(pcstr args) override
    {
        string256 section{}, rarity_name{};
        const int n = sscanf(args, "%255s %255s", section, rarity_name);
        if (n < 1)
        {
            InvalidSyntax();
            return;
        }
        loot::ERarity rarity = loot::eCommon;
        if (n == 2 && !ParseRarity(rarity_name, rarity))
        {
            Msg("! loot: unknown rarity '%s'", rarity_name);
            return;
        }
        SpawnForActor(section, n == 2 ? int(rarity) : -1);
    }
    void Info(TInfo& I) override { xr_strcpy(I, "<item section> [rarity] spawn a rolled item into your inventory"); }
};

class CCC_LootItemInfo : public IConsole_Command
{
public:
    CCC_LootItemInfo(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    void Execute(pcstr) override
    {
        PIItem item = g_actor ? g_actor->inventory().ActiveItem() : nullptr;
        if (!item)
        {
            Msg("! loot: hold an item first");
            return;
        }
        Msg("- %s [%s]: %s, cost %u, weight %.2f kg", item->NameItem(), item->object().cNameSect().c_str(),
            loot::RarityName(item->m_loot_rarity), item->Cost(), item->Weight());
        for (const auto& [id, value] : item->m_loot_affixes)
            Msg("    %s = %+.3f", id.c_str(), value);
    }
    void Info(TInfo& I) override { xr_strcpy(I, "show the rolls of the item in your hands"); }
};

class CCC_LootReroll : public IConsole_Command
{
public:
    CCC_LootReroll(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    void Execute(pcstr args) override
    {
        PIItem item = g_actor ? g_actor->inventory().ActiveItem() : nullptr;
        if (!item)
        {
            Msg("! loot: hold an item first");
            return;
        }
        loot::ERarity rarity = loot::ERarity(item->m_loot_rarity);
        if (args && args[0] && !ParseRarity(args, rarity))
        {
            Msg("! loot: unknown rarity '%s'", args);
            return;
        }
        const shared_str section = item->object().cNameSect();
        item->object().DestroyObject();
        SpawnForActor(section.c_str(), int(rarity));
    }
    void Info(TInfo& I) override { xr_strcpy(I, "[rarity] replace the item in your hands with a new roll"); }
};
} // namespace

void RegisterLootConsoleCommands()
{
    CMD1(CCC_LootSpawn, "rpg_spawn");
    CMD1(CCC_LootItemInfo, "rpg_item_info");
    CMD1(CCC_LootReroll, "rpg_reroll");
}
