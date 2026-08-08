#include "InputMap.h"

#include <wx/config.h>

namespace
{
    const std::array<GbaButton, InputMap::BUTTON_COUNT> kAllButtons = {
        GbaButton::Up,
        GbaButton::Down,
        GbaButton::Left,
        GbaButton::Right,
        GbaButton::A,
        GbaButton::B,
        GbaButton::L,
        GbaButton::R,
        GbaButton::Start,
        GbaButton::Select
    };
}

InputMap::InputMap()
{
    RestoreDefaults();
}

size_t InputMap::IndexOf(GbaButton button)
{
    return static_cast<size_t>(button);
}

const std::array<GbaButton, InputMap::BUTTON_COUNT>& InputMap::AllButtons()
{
    return kAllButtons;
}

int InputMap::DefaultKeyFor(GbaButton button)
{
    //this is the best layout and if you disagree i will hurt you
    switch (button)
    {
        case GbaButton::Up:     return WXK_UP;
        case GbaButton::Down:   return WXK_DOWN;
        case GbaButton::Left:   return WXK_LEFT;
        case GbaButton::Right:  return WXK_RIGHT;
        case GbaButton::A:      return 'Z';
        case GbaButton::B:      return 'X';
        case GbaButton::L:      return 'A';
        case GbaButton::R:      return 'S';
        case GbaButton::Start:  return WXK_RETURN;
        case GbaButton::Select: return WXK_BACK;
        default:                return UNBOUND;
    }
}

wxString InputMap::GetButtonName(GbaButton button)
{
    switch (button)
    {
        case GbaButton::Up:     return "Up";
        case GbaButton::Down:   return "Down";
        case GbaButton::Left:   return "Left";
        case GbaButton::Right:  return "Right";
        case GbaButton::A:      return "A";
        case GbaButton::B:      return "B";
        case GbaButton::L:      return "L";
        case GbaButton::R:      return "R";
        case GbaButton::Start:  return "Start";
        case GbaButton::Select: return "Select";
        default:                return "?";
    }
}

wxString InputMap::ConfigKeyFor(GbaButton button)
{
    return "/Input/" + GetButtonName(button);
}

wxString InputMap::DescribeKey(int keyCode)
{
    if (keyCode == UNBOUND)
        return "(none)";

    switch (keyCode)
    {
        case WXK_UP:       return "Up Arrow";
        case WXK_DOWN:     return "Down Arrow";
        case WXK_LEFT:     return "Left Arrow";
        case WXK_RIGHT:    return "Right Arrow";
        case WXK_RETURN:   return "Enter";
        case WXK_BACK:     return "Backspace";
        case WXK_SPACE:    return "Space";
        case WXK_TAB:      return "Tab";
        case WXK_SHIFT:    return "Shift";
        case WXK_CONTROL:  return "Ctrl";
        case WXK_ALT:      return "Alt";
        case WXK_HOME:     return "Home";
        case WXK_END:      return "End";
        case WXK_INSERT:   return "Insert";
        case WXK_DELETE:   return "Delete";
        case WXK_PAGEUP:   return "Page Up";
        case WXK_PAGEDOWN: return "Page Down";
        default:           break;
    }

    if (keyCode >= WXK_F1 && keyCode <= WXK_F24)
        return wxString::Format("F%d", keyCode - WXK_F1 + 1);

    if (keyCode >= WXK_NUMPAD0 && keyCode <= WXK_NUMPAD9)
        return wxString::Format("Numpad %d", keyCode - WXK_NUMPAD0);

    if (keyCode > 32 && keyCode < 127)
        return wxString::Format("%c", static_cast<wxChar>(keyCode));

    return wxString::Format("Key %d", keyCode);
}

void InputMap::RestoreDefaults()
{
    for (GbaButton button : kAllButtons)
        keyForButton[IndexOf(button)] = DefaultKeyFor(button);
}

int InputMap::GetKeyFor(GbaButton button) const
{
    return keyForButton[IndexOf(button)];
}

void InputMap::Bind(GbaButton button, int keyCode)
{
    if (keyCode != UNBOUND)
    {
        for (GbaButton other : kAllButtons)
        {
            if (other != button && keyForButton[IndexOf(other)] == keyCode)
                keyForButton[IndexOf(other)] = UNBOUND;
        }
    }

    keyForButton[IndexOf(button)] = keyCode;
}

void InputMap::LoadFromConfig()
{
    wxConfigBase* config = wxConfigBase::Get();
    if (!config)
        return;

    for (GbaButton button : kAllButtons)
    {
        long stored = 0;
        if (config->Read(ConfigKeyFor(button), &stored))
            keyForButton[IndexOf(button)] = static_cast<int>(stored);
    }
}

void InputMap::SaveToConfig() const
{
    wxConfigBase* config = wxConfigBase::Get();
    if (!config)
        return;

    for (GbaButton button : kAllButtons)
        config->Write(ConfigKeyFor(button), static_cast<long>(keyForButton[IndexOf(button)]));

    config->Flush();
}
