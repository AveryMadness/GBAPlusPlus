#pragma once
#include <wx/wx.h>
#include <array>

#include "../AGB/Input.h"

class InputMap
{
public:
    static constexpr size_t BUTTON_COUNT = 10;

    InputMap();

    void LoadFromConfig();
    void SaveToConfig() const;
    void RestoreDefaults();

    int GetKeyFor(GbaButton button) const;

    void Bind(GbaButton button, int keyCode);

    static const std::array<GbaButton, BUTTON_COUNT>& AllButtons();
    static wxString GetButtonName(GbaButton button);
    static wxString DescribeKey(int keyCode);

    static constexpr int UNBOUND = 0;

private:
    std::array<int, BUTTON_COUNT> keyForButton{};

    static size_t IndexOf(GbaButton button);
    static int DefaultKeyFor(GbaButton button);
    static wxString ConfigKeyFor(GbaButton button);
};
