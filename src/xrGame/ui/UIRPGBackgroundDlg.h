#pragma once

// "Choose your background" dialog, offered when the player has none yet
// (new game, or a save made before the RPG systems). Layout: ui_rpg_background.xml

#include "UIDialogWnd.h"
#include "xrUICore/Callbacks/UIWndCallback.h"

class CUIRPGBackgroundDlg final : public CUIDialogWnd, public CUIWndCallback
{
public:
    CUIRPGBackgroundDlg();

    bool Init();
    void SendMessage(CUIWindow* pWnd, s16 msg, void* pData = nullptr) override;

    pcstr GetDebugType() override { return "CUIRPGBackgroundDlg"; }

private:
    void OnButton(CUIWindow* wnd, void* data);
    void Choose(u8 background);

    xr_vector<std::pair<CUIWindow*, u8>> m_buttons; // button -> rpg::EBackground
};
