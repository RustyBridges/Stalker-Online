#include "StdAfx.h"

#include "UIRPGBackgroundDlg.h"
#include "UIHelper.h"
#include "UIXmlInit.h"
#include "Actor.h"
#include "ActorRPG.h"
#include "xrUICore/Buttons/UI3tButton.h"
#include "xrUICore/Static/UIStatic.h"

constexpr pcstr RPG_BACKGROUND_XML = "ui_rpg_background.xml";

using namespace rpg;

namespace
{
struct Choice
{
    EBackground background;
    pcstr button;
    pcstr description;
};

constexpr Choice choices[] = {
    { eBgSoldier, "btn_soldier", "desc_soldier" },
    { eBgSmuggler, "btn_smuggler", "desc_smuggler" },
    { eBgScientist, "btn_scientist", "desc_scientist" },
    { eBgDrifter, "btn_drifter", "desc_drifter" },
};
} // namespace

CUIRPGBackgroundDlg::CUIRPGBackgroundDlg() : CUIDialogWnd("CUIRPGBackgroundDlg") {}

bool CUIRPGBackgroundDlg::Init()
{
    CUIXml xml;
    if (!xml.Load(CONFIG_PATH, UI_PATH, UI_PATH_DEFAULT, RPG_BACKGROUND_XML, false))
        return false;

    CUIXmlInit::InitWindow(xml, "main_wnd", 0, this);
    std::ignore = UIHelper::CreateStatic(xml, "form", this, false);
    std::ignore = UIHelper::CreateStatic(xml, "caption", this, false);
    std::ignore = UIHelper::CreateStatic(xml, "intro", this, false);

    for (const Choice& c : choices)
    {
        CUI3tButton* btn = UIHelper::Create3tButton(xml, c.button, this, false);
        std::ignore = UIHelper::CreateStatic(xml, c.description, this, false);
        if (!btn)
            continue;

        if (!btn->GetText() || !btn->GetText()[0])
            btn->SetText(BackgroundName(c.background));

        Register(btn);
        m_buttons.emplace_back(btn, u8(c.background));
        AddCallback(btn, BUTTON_CLICKED, CUIWndCallback::void_function(this, &CUIRPGBackgroundDlg::OnButton));
    }

    std::ignore = UIHelper::CreateStatic(xml, "footer", this, false);
    return true;
}

void CUIRPGBackgroundDlg::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
    CUIWndCallback::OnEvent(pWnd, msg, pData);
}

void CUIRPGBackgroundDlg::OnButton(CUIWindow* wnd, void* /*data*/)
{
    for (const auto& [button, bg] : m_buttons)
    {
        if (button == wnd)
        {
            Choose(bg);
            return;
        }
    }
}

void CUIRPGBackgroundDlg::Choose(u8 background)
{
    HideDialog();

    CActor* actor = g_actor;
    if (!actor)
        return;

    const EBackground bg = EBackground(background);
    actor->RPG().SetBackground(bg);
    Msg("* RPG: background chosen: %s", BackgroundName(bg));
}
