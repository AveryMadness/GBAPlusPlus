// MemoryViewerFrame.cpp
#include "MemoryViewerFrame.h"
#include <sstream>
#include <iomanip>

wxBEGIN_EVENT_TABLE(MemoryViewerFrame, wxFrame)
    EVT_CLOSE(MemoryViewerFrame::OnClose)
wxEND_EVENT_TABLE()

MemoryViewerFrame::MemoryViewerFrame(wxWindow* parent, MemoryBus* bus)
    : wxFrame(parent, wxID_ANY, "Memory Viewer", wxDefaultPosition, wxSize(900, 600))
    , memoryBus(bus)
    , viewOffset(0)
    , bytesPerRow(16)
    , rowHeight(20)
    , visibleRows(0)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Toolbar
    wxBoxSizer* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
    toolbarSizer->Add(new wxStaticText(this, wxID_ANY, "Go to address:"), 
                      0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    
    gotoInput = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(150, -1));
    toolbarSizer->Add(gotoInput, 0, wxALL, 5);
    
    wxButton* gotoBtn = new wxButton(this, wxID_ANY, "Go");
    gotoBtn->Bind(wxEVT_BUTTON, &MemoryViewerFrame::OnGoto, this);
    toolbarSizer->Add(gotoBtn, 0, wxALL, 5);
    
    // Quick jump buttons
    wxButton* biosBtn = new wxButton(this, wxID_ANY, "BIOS");
    biosBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ScrollToAddress(0x00000000); });
    toolbarSizer->Add(biosBtn, 0, wxALL, 5);
    
    wxButton* iwramBtn = new wxButton(this, wxID_ANY, "IWRAM");
    iwramBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ScrollToAddress(0x03000000); });
    toolbarSizer->Add(iwramBtn, 0, wxALL, 5);
    
    wxButton* romBtn = new wxButton(this, wxID_ANY, "ROM");
    romBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ScrollToAddress(0x08000000); });
    toolbarSizer->Add(romBtn, 0, wxALL, 5);
    
    mainSizer->Add(toolbarSizer, 0, wxEXPAND);
    
    // Memory display area
    memoryPanel = new wxWindow(this, wxID_ANY);
    memoryPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    memoryPanel->Bind(wxEVT_PAINT, &MemoryViewerFrame::OnPaint, this);
    memoryPanel->Bind(wxEVT_MOUSEWHEEL, &MemoryViewerFrame::OnMouseWheel, this);
    memoryPanel->Bind(wxEVT_LEFT_DOWN, &MemoryViewerFrame::OnLeftClick, this);
    
    mainSizer->Add(memoryPanel, 1, wxEXPAND);
    
    SetSizer(mainSizer);
    
    monoFont = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    
    CreateStatusBar();
    SetStatusText("Ready");
    
    CallAfter([this]() { Refresh(); });
}

void MemoryViewerFrame::OnGoto(wxCommandEvent& event) {
    wxString text = gotoInput->GetValue();
    
    // Remove 0x prefix if present
    if (text.StartsWith("0x") || text.StartsWith("0X")) {
        text = text.Mid(2);
    }
    
    unsigned long address;
    if (text.ToULong(&address, 16)) {
        ScrollToAddress(address);
    } else {
        wxMessageBox("Invalid hexadecimal address", "Error", wxICON_ERROR);
    }
}

void MemoryViewerFrame::ScrollToAddress(uint32_t address) {
    viewOffset = address & ~0xF; // Align to 16 bytes
    Refresh();
}

void MemoryViewerFrame::OnSize(wxSizeEvent& event) {
    if (memoryPanel) {
        wxSize size = memoryPanel->GetClientSize();
        visibleRows = std::max(1, (size.GetHeight() / rowHeight) - 2); // -2 for header and padding
        Refresh();
    }
    event.Skip();
}

void MemoryViewerFrame::OnMouseWheel(wxMouseEvent& event) {
    int delta = event.GetWheelRotation();
    int lines = delta / event.GetWheelDelta() * 3;
    
    int64_t newOffset = (int64_t)viewOffset - (lines * bytesPerRow);
    newOffset = std::max(0LL, std::min(newOffset, (int64_t)(0x10000000 - visibleRows * bytesPerRow)));
    
    viewOffset = (uint32_t)newOffset;
    viewOffset &= ~0xF;
    
    Refresh();
}

void MemoryViewerFrame::OnLeftClick(wxMouseEvent& event) {
    // TODO: Implement address selection
    event.Skip();
}

wxColour MemoryViewerFrame::GetColorForAddress(uint32_t address) {
    uint8_t region = address >> 24;
    
    switch (region) {
        case 0x00:
            if (address < 0x00004000)
                return wxColour(100, 149, 237); // BIOS - Cornflower blue
            else
                return wxColour(80, 80, 80);    // Unmapped
        case 0x02: return wxColour(144, 238, 144); // EWRAM - Light green
        case 0x03: return wxColour(255, 218, 185); // IWRAM - Peach
        case 0x04:
            if ((address & 0xFFFFFF) < 0x000400)
                return wxColour(255, 182, 193); // I/O - Light pink
            else
                return wxColour(80, 80, 80);
        case 0x05: return wxColour(221, 160, 221); // Palette - Plum
        case 0x06: return wxColour(173, 216, 230); // VRAM - Light blue
        case 0x07: return wxColour(255, 222, 173); // OAM - Navajo white
        case 0x08: case 0x09: return wxColour(255, 250, 205); // ROM WS0
        case 0x0A: case 0x0B: return wxColour(255, 245, 157); // ROM WS1
        case 0x0C: case 0x0D: return wxColour(255, 239, 127); // ROM WS2
        case 0x0E: case 0x0F: return wxColour(216, 191, 216); // SRAM
        default: return wxColour(80, 80, 80);
    }
}

void MemoryViewerFrame::OnPaint(wxPaintEvent& event) {
    if (!memoryPanel) return;
    wxPaintDC dc(memoryPanel);
    RenderMemory(dc);
}

void MemoryViewerFrame::RenderMemory(wxDC& dc) {
    if (!memoryBus || !memoryPanel) return;
    
    dc.SetFont(monoFont);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();
    
    wxSize size = memoryPanel->GetClientSize();
    int yPos = 10;  // Add top padding
    
    // Header with background
    dc.SetTextForeground(*wxBLACK);
    dc.SetBrush(wxBrush(wxColour(200, 200, 200)));
    dc.DrawRectangle(0, yPos - 5, size.GetWidth(), rowHeight + 5);
    
    std::stringstream header;
    header << "Address        ";
    for (int i = 0; i < bytesPerRow; i++) {
        header << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << i << " ";
    }
    header << "  ASCII";
    dc.DrawText(header.str(), 10, yPos);
    yPos += rowHeight + 5;
    
    // Memory rows
    uint32_t currentAddress = viewOffset;
    
    for (int row = 0; row < visibleRows && currentAddress < 0x10000000; row++) {
        // Address column
        std::stringstream addrStr;
        addrStr << "0x" << std::hex << std::uppercase 
                << std::setw(8) << std::setfill('0') << currentAddress;
        dc.SetTextForeground(*wxBLACK);
        dc.DrawText(addrStr.str(), 10, yPos);
        
        // Hex bytes
        int xOffset = 120;
        std::string ascii;
        
        for (int col = 0; col < bytesPerRow; col++) {
            uint32_t addr = currentAddress + col;
            uint8_t byte = memoryBus->read8(addr);
            
            // Set color based on memory region
            wxColour color = GetColorForAddress(addr);
            dc.SetTextForeground(color);
            
            // Render hex byte
            std::stringstream byteStr;
            byteStr << std::hex << std::uppercase 
                    << std::setw(2) << std::setfill('0') << (int)byte;
            dc.DrawText(byteStr.str(), xOffset + col * 30, yPos);
            
            // Build ASCII
            if (byte >= 32 && byte < 127) {
                ascii += (char)byte;
            } else {
                ascii += '.';
            }
        }
        
        // ASCII column
        dc.SetTextForeground(wxColour(100, 100, 100));
        dc.DrawText(ascii, xOffset + bytesPerRow * 30 + 20, yPos);
        
        yPos += rowHeight;
        currentAddress += bytesPerRow;
    }
    
    // Update status
    std::stringstream status;
    status << "Viewing: 0x" << std::hex << std::uppercase 
           << std::setw(8) << std::setfill('0') << viewOffset;
    SetStatusText(status.str());
}

void MemoryViewerFrame::UpdateDisplay() {
    Refresh();
}

void MemoryViewerFrame::OnClose(wxCloseEvent& event) {
    event.Veto();  // Don't destroy the window
    Hide();        // Just hide it instead
}