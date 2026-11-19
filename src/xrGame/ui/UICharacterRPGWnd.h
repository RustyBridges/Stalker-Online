#pragma once

// PDA "Character" tab: background, level and the RPG skills (see ActorRPG.h).
// Layout: pda_character_rpg.xml / pda_character_rpg_16.xml

#include "xrUICore/Windows/UIWindow.h"

class CUIStatic;
class CUIProgressBar;
class CUIXml;

class CUICharacterRPGWnd final : public CUIWindow
{
    using inherited = CUIWindow;

    struct SkillRow
    {
        CUIStatic* name{};
        CUIStatic* value{};
        CUIProgressBar* bar{};
        CUIStatic* effect{};
    };

    CUIStatic* m_background_value{};
    CUIStatic* m_level_value{};
    CUIStatic* m_next_level_value{};
    xr_vector<SkillRow> m_rows;

    u32 m_last_update{};

public:
    CUICharacterRPGWnd() : CUIWindow("CUICharacterRPGWnd") {}

    bool Init();
    void Show(bool status) override;
    void Update() override;

    pcstr GetDebugType() override { return "CUICharacterRPGWnd"; }

private:
    void Refresh();
};
