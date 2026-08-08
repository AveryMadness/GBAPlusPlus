#include "InputSettingsDialog.h"

#include <wx/statline.h>

InputSettingsDialog::InputSettingsDialog(wxWindow* parent, const InputMap& current)
    : wxDialog(parent, wxID_ANY, "Configure Input", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE)
    , working(current)
{
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    hintLabel = new wxStaticText(this, wxID_ANY,
        "Click a key to rebind it, then press the new key. Esc cancels a rebind.");
    root->Add(hintLabel, 0, wxALL, 10);

    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 8, 12);
    grid->AddGrowableCol(1, 1);

    for (GbaButton button : InputMap::AllButtons())
    {
        grid->Add(new wxStaticText(this, wxID_ANY, InputMap::GetButtonName(button)),
                  0, wxALIGN_CENTER_VERTICAL);

        wxButton* keyButton = new wxButton(this, wxID_ANY, "", wxDefaultPosition,
                                           wxSize(160, -1));

        //wx hands the click to a plain lambda, so capture which pad button this row drives
        keyButton->Bind(wxEVT_BUTTON, [this, button](wxCommandEvent&) {
            OnRebindClicked(button);
        });

        keyButtons[static_cast<size_t>(button)] = keyButton;
        grid->Add(keyButton, 1, wxEXPAND);
    }

    root->Add(grid, 1, wxEXPAND | wxALL, 10);

    wxBoxSizer* buttonRow = new wxBoxSizer(wxHORIZONTAL);

    wxButton* defaultsButton = new wxButton(this, wxID_ANY, "Restore Defaults");
    defaultsButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        StopListening();
        working.RestoreDefaults();
        RefreshAllRows();
    });

    buttonRow->Add(defaultsButton, 0);
    buttonRow->AddStretchSpacer(1);
    buttonRow->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0, wxRIGHT, 6);
    buttonRow->Add(new wxButton(this, wxID_OK, "OK"), 0);

    root->Add(buttonRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    SetSizerAndFit(root);
    CentreOnParent();

    RefreshAllRows();

    //CHAR_HOOK sees the keypress before any control acts on it, which is the only way
    //to capture Tab/Enter/arrows as bindings instead of having them drive the dialog
    Bind(wxEVT_CHAR_HOOK, &InputSettingsDialog::OnCharHook, this);
}

const InputMap& InputSettingsDialog::GetResult() const
{
    return working;
}

void InputSettingsDialog::RefreshRow(GbaButton button)
{
    wxButton* keyButton = keyButtons[static_cast<size_t>(button)];
    if (!keyButton)
        return;

    if (listening && button == listeningFor)
        keyButton->SetLabel("Press a key...");
    else
        keyButton->SetLabel(InputMap::DescribeKey(working.GetKeyFor(button)));
}

void InputSettingsDialog::RefreshAllRows()
{
    for (GbaButton button : InputMap::AllButtons())
        RefreshRow(button);
}

void InputSettingsDialog::OnRebindClicked(GbaButton button)
{
    const bool wasListening = listening;
    const GbaButton previous = listeningFor;

    listening = true;
    listeningFor = button;

    if (wasListening && previous != button)
        RefreshRow(previous);

    RefreshRow(button);
}

void InputSettingsDialog::StopListening()
{
    if (!listening)
        return;

    const GbaButton previous = listeningFor;
    listening = false;
    RefreshRow(previous);
}

void InputSettingsDialog::OnCharHook(wxKeyEvent& event)
{
    if (!listening)
    {
        event.Skip();
        return;
    }

    const int keyCode = event.GetKeyCode();

    if (keyCode == WXK_ESCAPE)
    {
        StopListening();
        return;
    }

    const GbaButton target = listeningFor;

    //a key stolen from another row has to be redrawn too, so refresh everything
    working.Bind(target, keyCode);
    listening = false;
    RefreshAllRows();
}
