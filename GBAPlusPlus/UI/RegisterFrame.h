// RegisterFrame.h
#pragma once
#include <wx/wx.h>
#include "../AGB/ARMRegisters.h"

class RegisterFrame : public wxFrame {
public:
    RegisterFrame(wxWindow* parent, ARMRegisters* registers);
    void UpdateDisplay();

private:
    ARMRegisters* registers;
    wxPanel*      registerPanel;

    void OnPaint(wxPaintEvent& event);
    void DrawRegisters(wxDC& dc);
};
