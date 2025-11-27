#pragma once
#include <wx/wx.h>
#include "../AGB/ARMRegisters.h"

class RegisterFrame : public wxFrame {
public:
    RegisterFrame(wxWindow* parent, ARMRegisters* regs);
    
    void UpdateDisplay();
    
private:
    ARMRegisters* registers;
    wxTextCtrl* textCtrl;
};