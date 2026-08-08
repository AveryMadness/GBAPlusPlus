// RegisterFrame.h
#pragma once
#include <wx/wx.h>
#include <mutex>
#include "../AGB/ARMRegisters.h"

class MemoryBus;

class RegisterFrame : public wxFrame {
public:
    RegisterFrame(wxWindow* parent, ARMRegisters* registers, MemoryBus* memoryBus,
                  std::mutex* dataMutex);
    void UpdateDisplay();

private:
    ARMRegisters* registers;
    MemoryBus*    memoryBus;
    std::mutex*   dataMutex;
    wxPanel*      registerPanel;

    void OnPaint(wxPaintEvent& event);
    void DrawRegisters(wxDC& dc);
};
