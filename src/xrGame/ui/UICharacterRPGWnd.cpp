#include "StdAfx.h"

#include "UICharacterRPGWnd.h"
#include "UIHelper.h"
#include "UIXmlInit.h"
#include "Actor.h"
#include "ActorRPG.h"
#include "xrUICore/ProgressBar/UIProgressBar.h"
#include "xrUICore/Static/UIStatic.h"

constexpr pcstr PDA_CHARACTER_RPG_XML = "pda_character_rpg.xml";

using namespace rpg;

namespace
{
// Percentage change of a multiplier, e.g. 0.88 -> -12, 1.3 -> +30
int Percent(float k) { return iFloor((k - 1.f) * 100.f + (k >= 1.f ? 0.5f : -0.5f)); }

void EffectText(const CActorRPG& rpg, ESkill skill, string256& out)
{
    if (rpg.Mastery(skill) <= 0.f)
    {
        xr_strcpy(out, "No effect yet");
        return;
    }

    switch (skill)
    {
    case eMarksmanship:
        xr_sprintf(out, "Weapon spread and recoil %+d%%", Percent(rpg.WeaponDispersionK()));
        break;
    case eMelee: xr_sprintf(out, "Knife damage %+d%%", Percent(rpg.MeleeDamageK())); break;
    case eEndurance:
        xr_sprintf(out, "Carry weight +%.1f kg, stamina use %+d%%", rpg.CarryWeightBonus(),
            Percent(rpg.StaminaDrainK()));
        break;
    case eFirstAid: xr_sprintf(out, "Healing from medical items %+d%%", Percent(rpg.HealK())); break;
    case eResilience:
        xr_sprintf(out, "Radiation, anomaly and psi damage %+d%%", Percent(rpg.HazardDamageK()));
        break;
    case eBarter:
        xr_sprintf(out, "Buying %+d%%, selling %+d%%", Percent(rpg.BuyPriceK()), Percent(rpg.SellPriceK()));
        break;
    case eArtefactLore: xr_sprintf(out, "Artefact sale value %+d%%", Percent(rpg.ArtefactSellK())); break;
    case eStealth: xr_sprintf(out, "Noticed by enemies %+d%% slower", -Percent(rpg.DetectionK())); break;
    default: out[0] = 0; break;
    }
}
} // namespace

bool CUICharacterRPGWnd::Init()
{
    CUIXml xml;
    if (!xml.Load(CONFIG_PATH, UI_PATH, UI_PATH_DEFAULT, PDA_CHARACTER_RPG_XML, false))
        return false;

    CUIXmlInit::InitWindow(xml, "main_wnd", 0, this);
    const float row_height = xml.ReadAttribFlt("main_wnd", 0, "row_height", 40.f);

    std::ignore = UIHelper::CreateFrameWindow(xml, "background", this, false);
    std::ignore = UIHelper::CreateStatic(xml, "caption", this, false);

    std::ignore = UIHelper::CreateStatic(xml, "background_caption", this, false);
    m_background_value = UIHelper::CreateStatic(xml, "background_value", this, false);
    std::ignore = UIHelper::CreateStatic(xml, "level_caption", this, false);
    m_level_value = UIHelper::CreateStatic(xml, "level_value", this, false);
    m_next_level_value = UIHelper::CreateStatic(xml, "next_level_value", this, false);

    std::ignore = UIHelper::CreateStatic(xml, "skills_caption", this, false);

    // One row per skill, built from the "skill_*" templates and moved down by row_height.
    m_rows.resize(eSkillCount);
    for (u8 i = 0; i < eSkillCount; ++i)
    {
        SkillRow& row = m_rows[i];
        row.name = UIHelper::CreateStatic(xml, "skill_name", this, false);
        row.value = UIHelper::CreateStatic(xml, "skill_value", this, false);
        row.bar = UIHelper::CreateProgressBar(xml, "skill_bar", this, false);
        row.effect = UIHelper::CreateStatic(xml, "skill_effect", this, false);

        const float dy = row_height * i;
        auto offset = [dy](CUIWindow* wnd)
        {
            if (!wnd)
                return;
            Fvector2 pos = wnd->GetWndPos();
            pos.y += dy;
            wnd->SetWndPos(pos);
        };
        offset(row.name);
        offset(row.value);
        offset(row.bar);
        offset(row.effect);

        if (row.name)
            row.name->SetText(SkillName(ESkill(i)));
        if (row.bar)
            row.bar->SetRange(0.f, float(SKILL_MAX));
    }
    return true;
}

void CUICharacterRPGWnd::Show(bool status)
{
    inherited::Show(status);
    if (status)
        Refresh();
}

void CUICharacterRPGWnd::Update()
{
    inherited::Update();
    if (Device.dwTimeGlobal - m_last_update > 500)
        Refresh();
}

void CUICharacterRPGWnd::Refresh()
{
    m_last_update = Device.dwTimeGlobal;

    const CActor* actor = Actor();
    if (!actor)
        return;
    const CActorRPG& rpg = actor->RPG();

    string256 buf;
    if (m_background_value)
        m_background_value->SetText(rpg.Background() == eBgNone ? "None chosen" : BackgroundName(rpg.Background()));
    if (m_level_value)
    {
        xr_sprintf(buf, "%u", rpg.Level());
        m_level_value->SetText(buf);
    }
    if (m_next_level_value)
    {
        xr_sprintf(buf, "%u skill points to next level", rpg.PointsToNextLevel());
        m_next_level_value->SetText(buf);
    }

    for (u8 i = 0; i < eSkillCount && i < m_rows.size(); ++i)
    {
        const ESkill skill = ESkill(i);
        SkillRow& row = m_rows[i];
        if (row.value)
        {
            if (rpg.Skill(skill) >= SKILL_MAX)
                xr_sprintf(buf, "%u (max)", u32(rpg.Skill(skill)));
            else
                xr_sprintf(buf, "%u  (%d%%)", u32(rpg.Skill(skill)), iFloor(rpg.Progress(skill) * 100.f));
            row.value->SetText(buf);
        }
        if (row.bar)
            row.bar->ForceSetProgressPos(float(rpg.Skill(skill)));
        if (row.effect)
        {
            EffectText(rpg, skill, buf);
            row.effect->SetText(buf);
        }
    }
}
