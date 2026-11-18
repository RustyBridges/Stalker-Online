#include "StdAfx.h"

#include "ActorRPG.h"
#include "Actor.h"
#include "game_news.h"
#include "xrEngine/XR_IOConsole.h"
#include "xrEngine/xr_ioc_cmd.h"

using namespace rpg;

namespace rpg
{
float g_gain_mult = 1.f;

namespace
{
struct SkillInfo
{
    pcstr id;
    pcstr name;
};

constexpr SkillInfo skill_info[eSkillCount] = {
    { "marksmanship", "Marksmanship" },
    { "melee", "Melee" },
    { "endurance", "Endurance" },
    { "first_aid", "First aid" },
    { "resilience", "Resilience" },
    { "barter", "Barter" },
    { "artefact_lore", "Artefact lore" },
    { "stealth", "Stealth" },
};

struct BackgroundInfo
{
    pcstr id;
    pcstr name;
    ESkill head_start[2];
};

constexpr BackgroundInfo background_info[eBgCount] = {
    { "none", "None", { eSkillCount, eSkillCount } },
    { "soldier", "Ex-soldier", { eMarksmanship, eEndurance } },
    { "smuggler", "Smuggler", { eBarter, eStealth } },
    { "scientist", "Scientist's assistant", { eArtefactLore, eFirstAid } },
    { "drifter", "Drifter", { eEndurance, eResilience } },
};

constexpr int HEAD_START_POINTS = 15; // per head start skill
constexpr u32 POINTS_PER_LEVEL = 12; // skill points gained by use per character level
constexpr u8 SAVE_VERSION = 1; // v1: background, skills, points, found artefacts
} // namespace

pcstr SkillId(ESkill skill) { return skill < eSkillCount ? skill_info[skill].id : "?"; }
pcstr SkillName(ESkill skill) { return skill < eSkillCount ? skill_info[skill].name : "?"; }
pcstr BackgroundId(EBackground bg) { return bg < eBgCount ? background_info[bg].id : "?"; }
pcstr BackgroundName(EBackground bg) { return bg < eBgCount ? background_info[bg].name : "?"; }

bool ParseSkill(pcstr text, ESkill& skill)
{
    for (u8 i = 0; i < eSkillCount; ++i)
    {
        if (0 == xr_stricmp(text, skill_info[i].id))
        {
            skill = ESkill(i);
            return true;
        }
    }
    return false;
}

bool ParseBackground(pcstr text, EBackground& bg)
{
    for (u8 i = 0; i < eBgCount; ++i)
    {
        if (0 == xr_stricmp(text, background_info[i].id))
        {
            bg = EBackground(i);
            return true;
        }
    }
    return false;
}
} // namespace rpg

CActorRPG::CActorRPG(CActor* owner) : m_owner(owner) {}

void CActorRPG::SetBackground(EBackground bg)
{
    for (SkillState& s : m_skills)
        s = {};
    m_points_gained = 0;
    m_found_artefacts.clear();
    m_background = bg;

    for (const ESkill skill : background_info[bg].head_start)
    {
        if (skill < eSkillCount)
            AddPoints(skill, HEAD_START_POINTS, false);
    }
}

float CActorRPG::Mastery(ESkill skill) const
{
    return float(m_skills[skill].value - SKILL_MIN) / float(SKILL_MAX - SKILL_MIN);
}

u32 CActorRPG::Level() const { return 1 + m_points_gained / POINTS_PER_LEVEL; }
u32 CActorRPG::PointsToNextLevel() const { return POINTS_PER_LEVEL - m_points_gained % POINTS_PER_LEVEL; }

float CActorRPG::UsesForNextPoint(u8 value)
{
    // Early points come quickly, the last ones take a full playthrough:
    // ~2 uses at skill 1, ~14 at 50, ~51 at 99 (~1800 uses from 1 to 100).
    return 2.f + float(value) * float(value) / 200.f;
}

void CActorRPG::Train(ESkill skill, float uses)
{
    if (skill >= eSkillCount || uses <= 0.f)
        return;

    SkillState& s = m_skills[skill];
    if (s.value >= SKILL_MAX)
        return;

    const u32 old_level = Level();
    bool improved = false;

    s.progress += uses * g_gain_mult / UsesForNextPoint(s.value);
    while (s.progress >= 1.f && s.value < SKILL_MAX)
    {
        s.progress -= 1.f;
        ++s.value;
        ++m_points_gained;
        improved = true;
    }
    if (s.value >= SKILL_MAX)
        s.progress = 0.f;

    if (improved)
        OnSkillUp(skill, old_level);
}

void CActorRPG::TrainTimed(ESkill skill, float uses, u32 interval_ms)
{
    if (skill >= eSkillCount)
        return;
    const u32 now = Device.dwTimeGlobal;
    if (now - m_last_train_time[skill] < interval_ms)
        return;
    m_last_train_time[skill] = now;
    Train(skill, uses);
}

void CActorRPG::OnArtefactFound(u16 object_id)
{
    if (m_found_artefacts.insert(object_id).second)
        Train(eArtefactLore, 2.f);
}

void CActorRPG::AddPoints(ESkill skill, int points, bool counts_for_level)
{
    if (skill >= eSkillCount || points == 0)
        return;

    SkillState& s = m_skills[skill];
    const int new_value = std::clamp(int(s.value) + points, int(SKILL_MIN), int(SKILL_MAX));
    const int gained = new_value - int(s.value);
    s.value = u8(new_value);
    if (s.value >= SKILL_MAX)
        s.progress = 0.f;

    if (counts_for_level && gained > 0)
    {
        const u32 old_level = Level();
        m_points_gained += u32(gained);
        OnSkillUp(skill, old_level);
    }
}

// --- effects ------------------------------------------------------------------------

float CActorRPG::WeaponDispersionK() const { return 1.f - 0.35f * Mastery(eMarksmanship); }
float CActorRPG::WeaponRecoilK() const { return 1.f - 0.35f * Mastery(eMarksmanship); }
float CActorRPG::MeleeDamageK() const { return 1.f + 0.40f * Mastery(eMelee); }
float CActorRPG::CarryWeightBonus() const { return 15.f * Mastery(eEndurance); }
float CActorRPG::StaminaDrainK() const { return 1.f / (1.f + 0.30f * Mastery(eEndurance)); }
float CActorRPG::HealK() const { return 1.f + 0.40f * Mastery(eFirstAid); }
float CActorRPG::HazardDamageK() const { return 1.f - 0.25f * Mastery(eResilience); }
float CActorRPG::BuyPriceK() const { return 1.f - 0.20f * Mastery(eBarter); }
float CActorRPG::SellPriceK() const { return 1.f + 0.30f * Mastery(eBarter); }
float CActorRPG::ArtefactSellK() const { return 1.f + 0.30f * Mastery(eArtefactLore); }
float CActorRPG::DetectionK() const { return 1.f - 0.30f * Mastery(eStealth); }

// --- notifications ----------------------------------------------------------------

void CActorRPG::OnSkillUp(ESkill skill, u32 old_level)
{
    string256 text;
    xr_sprintf(text, "%s is now %u.", SkillName(skill), u32(m_skills[skill].value));
    Msg("* RPG: %s", text);
    SendNews("Skill improved", text);

    if (Level() > old_level)
    {
        xr_sprintf(text, "You are now level %u.", Level());
        Msg("* RPG: %s", text);
        SendNews("Experience", text);
    }
}

void CActorRPG::SendNews(pcstr caption, pcstr text) const
{
    if (!m_owner || m_owner != Actor())
        return;

    GAME_NEWS_DATA news;
    news.m_type = GAME_NEWS_DATA::eNews;
    news.news_caption = caption;
    news.news_text = text;
    news.texture_name = m_owner->IconName();
    m_owner->AddGameNews(std::move(news));
}

void CActorRPG::OnSpawn()
{
    if (m_background != eBgNone || !m_owner || m_owner != Actor())
        return;

    // Until the New Game dialog exists: tell the player how to pick a background.
    GAME_NEWS_DATA news;
    news.m_type = GAME_NEWS_DATA::eNews;
    news.news_caption = "Choose a background";
    news.news_text = "Open the console (~) and type rpg_background followed by soldier, smuggler, "
                     "scientist or drifter. Type rpg_sheet to see your character.";
    news.texture_name = m_owner->IconName();
    news.show_time = 15000;
    m_owner->AddGameNews_deffered(std::move(news), 5000);
}

void CActorRPG::LogSheet() const
{
    Msg("----- Character -----");
    Msg("Background: %s", BackgroundName(m_background));
    Msg("Level: %u (%u skill points to next level)", Level(), PointsToNextLevel());
    for (u8 i = 0; i < eSkillCount; ++i)
    {
        const SkillState& s = m_skills[i];
        Msg("  %-14s %3u  [%3.0f%%]", SkillName(ESkill(i)), u32(s.value), s.progress * 100.f);
    }
    Msg("Effects: spread x%.2f, recoil x%.2f, melee x%.2f, carry +%.1f kg, stamina drain x%.2f",
        WeaponDispersionK(), WeaponRecoilK(), MeleeDamageK(), CarryWeightBonus(), StaminaDrainK());
    Msg("         healing x%.2f, hazard damage x%.2f, buy x%.2f, sell x%.2f, artefacts x%.2f, detection x%.2f",
        HealK(), HazardDamageK(), BuyPriceK(), SellPriceK(), ArtefactSellK(), DetectionK());
    Msg("---------------------");
}

// --- persistence -------------------------------------------------------------------

void CActorRPG::save(NET_Packet& packet) const
{
    packet.w_u8(SAVE_VERSION);
    packet.w_u8(u8(m_background));
    packet.w_u8(u8(eSkillCount));
    for (const SkillState& s : m_skills)
    {
        packet.w_u8(s.value);
        packet.w_float(s.progress);
    }
    packet.w_u32(m_points_gained);

    packet.w_u32(u32(m_found_artefacts.size()));
    for (const u16 id : m_found_artefacts)
        packet.w_u16(id);
}

void CActorRPG::load(IReader& packet)
{
    const u8 version = packet.r_u8();
    if (version == 0 || version > SAVE_VERSION)
    {
        Msg("! RPG: unknown save data version %u, character sheet reset", u32(version));
        return;
    }

    const u8 bg = packet.r_u8();
    m_background = bg < eBgCount ? EBackground(bg) : eBgNone;

    const u8 count = packet.r_u8();
    for (u8 i = 0; i < count; ++i)
    {
        const u8 value = packet.r_u8();
        const float progress = packet.r_float();
        if (i < eSkillCount) // skills added in later versions keep their defaults
        {
            m_skills[i].value = std::clamp(value, SKILL_MIN, SKILL_MAX);
            m_skills[i].progress = std::clamp(progress, 0.f, 1.f);
        }
    }
    m_points_gained = packet.r_u32();

    m_found_artefacts.clear();
    const u32 artefacts = packet.r_u32();
    for (u32 i = 0; i < artefacts; ++i)
        m_found_artefacts.insert(packet.r_u16());
}

// --- console commands ------------------------------------------------------------

namespace
{
CActorRPG* ConsoleRPG()
{
    CActor* actor = Actor();
    if (!actor)
    {
        Msg("! RPG: load or start a game first");
        return nullptr;
    }
    return &actor->RPG();
}

class CCC_RPG_Sheet : public IConsole_Command
{
public:
    CCC_RPG_Sheet(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; }
    void Execute(pcstr) override
    {
        if (CActorRPG* rpg = ConsoleRPG())
            rpg->LogSheet();
    }
    void Info(TInfo& I) override { xr_strcpy(I, "show the character sheet"); }
};

class CCC_RPG_Add : public IConsole_Command
{
public:
    CCC_RPG_Add(pcstr name) : IConsole_Command(name) {}
    void Execute(pcstr args) override
    {
        CActorRPG* rpg = ConsoleRPG();
        if (!rpg)
            return;

        string64 skill_name{};
        int points = 0;
        ESkill skill;
        if (sscanf(args, "%63s %d", skill_name, &points) != 2 || !ParseSkill(skill_name, skill))
        {
            InvalidSyntax();
            return;
        }
        rpg->AddPoints(skill, points, true);
        rpg->LogSheet();
    }
    void Info(TInfo& I) override
    {
        xr_strcpy(I, "<skill> <points>, skills: marksmanship melee endurance first_aid resilience barter "
                     "artefact_lore stealth");
    }
};

class CCC_RPG_Background : public IConsole_Command
{
public:
    CCC_RPG_Background(pcstr name) : IConsole_Command(name) {}
    void Execute(pcstr args) override
    {
        CActorRPG* rpg = ConsoleRPG();
        if (!rpg)
            return;

        EBackground bg;
        if (!ParseBackground(args, bg))
        {
            InvalidSyntax();
            return;
        }
        rpg->SetBackground(bg);
        Msg("* RPG: background set to %s (skills reset)", BackgroundName(bg));
        rpg->LogSheet();
    }
    void Info(TInfo& I) override { xr_strcpy(I, "soldier | smuggler | scientist | drifter | none (resets skills)"); }
};
} // namespace

void RegisterRPGConsoleCommands()
{
    CMD1(CCC_RPG_Sheet, "rpg_sheet");
    CMD1(CCC_RPG_Add, "rpg_add");
    CMD1(CCC_RPG_Background, "rpg_background");
    CMD4(CCC_Float, "rpg_gain_mult", &rpg::g_gain_mult, 0.f, 100.f);
}
