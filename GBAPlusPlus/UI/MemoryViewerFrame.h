// MemoryViewerFrame.h
#pragma once
#include <wx/wx.h>
#include <wx/grid.h>
#include "../AGB/MemoryBus.h"

class MemoryViewerFrame : public wxFrame {
public:
    MemoryViewerFrame(wxWindow* parent, MemoryBus* bus);
    
    void UpdateDisplay();
    void ScrollToAddress(uint32_t address);
    
private:
    void OnGoto(wxCommandEvent& event);
    void OnScroll(wxScrollWinEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnPaint(wxPaintEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnLeftClick(wxMouseEvent& event);
    void OnClose(wxCloseEvent& event);
    
    void RenderMemory(wxDC& dc);
    wxColour GetColorForAddress(uint32_t address);
    
    MemoryBus* memoryBus;
    uint32_t viewOffset;
    int bytesPerRow;
    int rowHeight;
    int visibleRows;
    
    wxTextCtrl* gotoInput;
    wxFont monoFont;
    wxWindow* memoryPanel;
    
    wxDECLARE_EVENT_TABLE();
};