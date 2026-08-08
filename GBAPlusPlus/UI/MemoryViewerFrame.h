// MemoryViewerFrame.h
#pragma once
#include <wx/wx.h>
#include <mutex>
#include "../AGB/MemoryBus.h"

class MemoryViewerFrame : public wxFrame {
public:
    MemoryViewerFrame(wxWindow* parent, MemoryBus* bus, std::mutex* dataMutex);
    void UpdateDisplay();

private:
    MemoryBus*   memoryBus;
    std::mutex*  dataMutex;
    wxWindow*    memoryPanel;
    wxTextCtrl*  gotoInput;
    wxFont       monoFont;

    uint32_t  viewOffset;
    int64_t   selectedAddress;   // -1 = none
    int       bytesPerRow;
    int       rowHeight;
    int       headerHeight;
    int       visibleRows;

    void RecalcVisibleRows(int panelHeight);
    void ScrollToAddress(uint32_t address);
    void RenderMemory(wxDC& dc);

    void OnGoto(wxCommandEvent& event);
    void OnPaint(wxPaintEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnLeftClick(wxMouseEvent& event);
    void OnMemPanelSize(wxSizeEvent& event);   // NEW - was missing
    void OnClose(wxCloseEvent& event);

    wxDECLARE_EVENT_TABLE();
};