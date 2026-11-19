#pragma once

// RPG progression for the player: a starting background and skills that
// improve by use (see doc/design/rpg-systems.md).
//
// Skills go from SKILL_MIN to SKILL_MAX. Effects scale with Mastery(),
// which is 0 at the minimum and 1 at the maximum.

class CActor;
class NET_Packet;
class IReader;
class CUIRPGBackgroundDlg;

namespace rpg
{
enum ESkill : u8
{
    eMarksmanship,
    eMelee,
    eEndurance,
    eFirstAid,
    eResilience,
    eBarter,
    eArtefactLore,
    eStealth,
    eSkillCount
};

enum EBackground : u8
{
    eBgNone,
    eBgSoldier,
    eBgSmuggler,
    eBgScientist,
    eBgDrifter,
    eBgCount
};

constexpr u8 SKILL_MIN = 1;
constexpr u8 SKILL_MAX = 100;

pcstr SkillId(ESkill skill); // console name, e.g. "marksmanship"
pcstr SkillName(ESkill skill); // display name, e.g. "Marksmanship"
pcstr BackgroundId(EBackground bg);
pcstr BackgroundName(EBackground bg);
bool ParseSkill(pcstr text, ESkill& skill);
bool ParseBackground(pcstr text, EBackground& bg);

// Global multiplier for skill gains (console: rpg_gain_mult), for testing.
extern float g_gain_mult;
// Testing mode (console: rpg_debug 1), readable from scripts as rpg.debug().
extern int g_debug;
} // namespace rpg

class CActorRPG
{
public:
    explicit CActorRPG(CActor* owner);
    ~CActorRPG();

    // --- state ---------------------------------------------------------------
    rpg::EBackground Background() const { return m_background; }
    void SetBackground(rpg::EBackground bg); // resets skills and applies the head start

    u8 Skill(rpg::ESkill skill) const { return m_skills[skill].value; }
    float Progress(rpg::ESkill skill) const { return m_skills[skill].progress; } // 0..1 toward next point
    float Mastery(rpg::ESkill skill) const;

    u32 Level() const;
    u32 PointsGained() const { return m_points_gained; }
    u32 PointsToNextLevel() const;

    // --- growth ----------------------------------------------------------------
    // One "use" is worth 1.0; the amount needed per point grows with the skill.
    void Train(rpg::ESkill skill, float uses);
    // Same, but at most once per interval_ms (real time) for this skill, for
    // continuous sources such as radiation, sneaking or automatic fire.
    void TrainTimed(rpg::ESkill skill, float uses, u32 interval_ms);
    // First pickup of an artefact (by object id) trains Artefact lore once.
    void OnArtefactFound(u16 object_id);
    void AddPoints(rpg::ESkill skill, int points, bool counts_for_level); // console / scripts
    void DebugSetLevel(u32 level); // console: rpg_set_level

    // --- effects (multipliers; 1 = no change) -------------------------------------
    float WeaponDispersionK() const; // Marksmanship: up to -35%
    float WeaponRecoilK() const; // Marksmanship: up to -35%
    float MeleeDamageK() const; // Melee: up to +40%
    float CarryWeightBonus() const; // Endurance: up to +15 kg
    float StaminaDrainK() const; // Endurance: about 30% more stamina
    float HealK() const; // First aid: up to +40%
    float HazardDamageK() const; // Resilience: up to -25% radiation / anomaly / psi damage
    float BuyPriceK() const; // Barter: pay up to 20% less
    float SellPriceK() const; // Barter: get up to 30% more
    float ArtefactSellK() const; // Artefact lore: artefacts sell for up to 30% more
    float DetectionK() const; // Stealth: enemies notice you up to 30% later

    // --- persistence -------------------------------------------------------------
    void save(NET_Packet& packet) const;
    void load(IReader& packet);

    void LogSheet() const;

    // Called when the player spawns (new game or loaded save).
    void OnSpawn();
    // Called every frame from CActor::UpdateCL (offers the background dialog).
    void Update();

private:
    struct SkillState
    {
        u8 value{ rpg::SKILL_MIN };
        float progress{};
    };

    CActor* m_owner;
    rpg::EBackground m_background{ rpg::eBgNone };
    SkillState m_skills[rpg::eSkillCount];
    u32 m_points_gained{}; // points gained by use (head start excluded); drives the level
    u32 m_last_train_time[rpg::eSkillCount]{}; // for TrainTimed, not saved
    xr_set<u16> m_found_artefacts; // artefacts already credited to Artefact lore

    u32 m_offer_background_at{}; // time to show the background dialog, 0 = not pending
    CUIRPGBackgroundDlg* m_background_dlg{};

    void SendBackgroundHint() const;

    static float UsesForNextPoint(u8 value);
    void OnSkillUp(rpg::ESkill skill, u32 old_level);
    void SendNews(pcstr caption, pcstr text) const;
};
