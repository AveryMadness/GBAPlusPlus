#pragma once
#include <wx/wx.h>
#include <array>

#include "InputMap.h"

//Rebinding UI. Edits a copy so Cancel really does discard - the caller only sees the
//new bindings if the dialog returns wxID_OK.
class InputSettingsDialog : public wxDialog
{
public:
    InputSettingsDialog(wxWindow* parent, const InputMap& current);

    const InputMap& GetResult() const;

private:
    void OnCharHook(wxKeyEvent& event);
    void OnRebindClicked(GbaButton button);
    void RefreshRow(GbaButton button);
    void RefreshAllRows();
    void StopListening();

    InputMap working;

    std::array<wxButton*, InputMap::BUTTON_COUNT> keyButtons{};
    wxStaticText* hintLabel = nullptr;

    //which button is waiting to swallow the next keypress, if any
    bool listening = false;
    GbaButton listeningFor = GbaButton::A;
};
