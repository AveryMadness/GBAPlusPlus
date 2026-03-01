// MemoryViewerFrame.cpp
#include "MemoryViewerFrame.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

// Dark theme colors (shared palette with RegisterFrame)
static const wxColour BG_DARK       (18,  20,  24);
static const wxColour BG_PANEL      (26,  29,  36);
static const wxColour BG_ROW_ALT    (28,  32,  40);
static const wxColour ACCENT_BLUE   (82,  139, 255);
static const wxColour TEXT_PRIMARY  (220, 225, 235);
static const wxColour TEXT_DIM      (90,  100, 120);
static const wxColour BORDER_COLOR  (45,  50,  62);
static const wxColour HEADER_BG     (22,  25,  32);

// Memory region colors (muted for dark theme)
struct RegionColor { wxColour text; wxColour bg; };

static RegionColor GetRegionColor(uint32_t address) {
    uint8_t region = address >> 24;
    switch (region) {
    case 0x00:
        if (address < 0x00004000)
            return { wxColour(130, 170, 255), wxColour(22, 28, 45) }; // BIOS - blue
        return { wxColour(70, 75, 85),  BG_DARK };
    case 0x02: return { wxColour(100, 210, 140), wxColour(18, 32, 22) }; // EWRAM - green
    case 0x03: return { wxColour(255, 200, 120), wxColour(38, 30, 16) }; // IWRAM - amber
    case 0x04:
        if ((address & 0xFFFFFF) < 0x400)
            return { wxColour(220, 130, 200), wxColour(35, 18, 35) }; // I/O - purple
        return { wxColour(70, 75, 85),  BG_DARK };
    case 0x05: return { wxColour(190, 140, 220), wxColour(30, 20, 38) }; // Palette - violet
    case 0x06: return { wxColour(100, 190, 220), wxColour(16, 28, 38) }; // VRAM - teal
    case 0x07: return { wxColour(220, 180, 110), wxColour(35, 28, 14) }; // OAM - yellow
    case 0x08: case 0x09:
    case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: return { wxColour(180, 200, 160), wxColour(22, 28, 18) }; // ROM - sage
    case 0x0E: case 0x0F: return { wxColour(200, 160, 160), wxColour(35, 20, 20) }; // SRAM - rose
    default:   return { wxColour(70, 75, 85),  BG_DARK };
    }
}

static const char* GetRegionName(uint32_t address) {
    uint8_t region = address >> 24;
    switch (region) {
    case 0x00: return address < 0x4000 ? "BIOS" : "----";
    case 0x02: return "EWRAM";
    case 0x03: return "IWRAM";
    case 0x04: return (address & 0xFFFFFF) < 0x400 ? "I/O" : "----";
    case 0x05: return "PAL";
    case 0x06: return "VRAM";
    case 0x07: return "OAM";
    case 0x08: case 0x09: return "ROM0";
    case 0x0A: case 0x0B: return "ROM1";
    case 0x0C: case 0x0D: return "ROM2";
    case 0x0E: case 0x0F: return "SRAM";
    default:   return "----";
    }
}

wxBEGIN_EVENT_TABLE(MemoryViewerFrame, wxFrame)
    EVT_CLOSE(MemoryViewerFrame::OnClose)
wxEND_EVENT_TABLE()

MemoryViewerFrame::MemoryViewerFrame(wxWindow* parent, MemoryBus* bus)
    : wxFrame(parent, wxID_ANY, "Memory Viewer",
              wxDefaultPosition, wxSize(920, 640),
              wxDEFAULT_FRAME_STYLE)
    , memoryBus(bus)
    , viewOffset(0)
    , bytesPerRow(16)
    , rowHeight(20)
    , visibleRows(20)
    , headerHeight(32)
    , selectedAddress(-1)
{
    SetBackgroundColour(BG_DARK);

    monoFont = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
#ifdef __WXMSW__
    wxFont cascadia(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                    false, "Cascadia Mono");
    if (cascadia.IsOk()) monoFont = cascadia;
#endif
    wxFont labelFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);

    wxBoxSizer* outerSizer = new wxBoxSizer(wxVERTICAL);

    // ── Title strip ──────────────────────────────────────────────────
    wxPanel* titlePanel = new wxPanel(this);
    titlePanel->SetBackgroundColour(BG_PANEL);
    titlePanel->SetMinSize(wxSize(-1, 36));
    wxBoxSizer* titleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* titleLabel = new wxStaticText(titlePanel, wxID_ANY, " \u2261  MEMORY VIEWER");
    titleLabel->SetFont(labelFont);
    titleLabel->SetForegroundColour(ACCENT_BLUE);
    titleLabel->SetBackgroundColour(BG_PANEL);
    titleSizer->Add(titleLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    titlePanel->SetSizer(titleSizer);
    outerSizer->Add(titlePanel, 0, wxEXPAND);

    // Separator
    wxPanel* sep1 = new wxPanel(this);
    sep1->SetBackgroundColour(BORDER_COLOR);
    sep1->SetMinSize(wxSize(-1, 1));
    outerSizer->Add(sep1, 0, wxEXPAND);

    // ── Toolbar ──────────────────────────────────────────────────────
    wxPanel* toolPanel = new wxPanel(this);
    toolPanel->SetBackgroundColour(BG_PANEL);
    toolPanel->SetMinSize(wxSize(-1, 42));
    wxBoxSizer* toolSizer = new wxBoxSizer(wxHORIZONTAL);

    auto MakeLabel = [&](wxPanel* parent, const wxString& text) {
        wxStaticText* lbl = new wxStaticText(parent, wxID_ANY, text);
        lbl->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        lbl->SetForegroundColour(TEXT_DIM);
        lbl->SetBackgroundColour(BG_PANEL);
        return lbl;
    };

    auto MakeJumpBtn = [&](wxPanel* parent, const wxString& label, uint32_t addr,
                           const wxColour& col) {
        wxButton* btn = new wxButton(parent, wxID_ANY, label,
                                     wxDefaultPosition, wxSize(60, 26));
        btn->SetBackgroundColour(wxColour(30, 34, 44));
        btn->SetForegroundColour(col);
        btn->SetFont(wxFont(8, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        btn->Bind(wxEVT_BUTTON, [this, addr](wxCommandEvent&) { ScrollToAddress(addr); });
        return btn;
    };

    gotoInput = new wxTextCtrl(toolPanel, wxID_ANY, "0x00000000",
                               wxDefaultPosition, wxSize(130, 26),
                               wxTE_PROCESS_ENTER);
    gotoInput->SetBackgroundColour(wxColour(30, 34, 44));
    gotoInput->SetForegroundColour(TEXT_PRIMARY);
    gotoInput->SetFont(monoFont);
    gotoInput->Bind(wxEVT_TEXT_ENTER, &MemoryViewerFrame::OnGoto, this);

    wxButton* gotoBtn = new wxButton(toolPanel, wxID_ANY, "Go",
                                     wxDefaultPosition, wxSize(42, 26));
    gotoBtn->SetBackgroundColour(ACCENT_BLUE);
    gotoBtn->SetForegroundColour(*wxWHITE);
    gotoBtn->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    gotoBtn->Bind(wxEVT_BUTTON, &MemoryViewerFrame::OnGoto, this);

    toolSizer->Add(MakeLabel(toolPanel, " Go to: "), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    toolSizer->Add(gotoInput, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
    toolSizer->Add(gotoBtn,   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    // Vertical divider
    wxPanel* vdiv = new wxPanel(toolPanel);
    vdiv->SetBackgroundColour(BORDER_COLOR);
    vdiv->SetMinSize(wxSize(1, 24));
    toolSizer->Add(vdiv, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    toolSizer->Add(MakeLabel(toolPanel, "Jump: "), 0, wxALIGN_CENTER_VERTICAL);
    toolSizer->Add(MakeJumpBtn(toolPanel, "BIOS",  0x00000000, wxColour(130, 170, 255)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    toolSizer->Add(MakeJumpBtn(toolPanel, "EWRAM", 0x02000000, wxColour(100, 210, 140)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    toolSizer->Add(MakeJumpBtn(toolPanel, "IWRAM", 0x03000000, wxColour(255, 200, 120)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    toolSizer->Add(MakeJumpBtn(toolPanel, "I/O",   0x04000000, wxColour(220, 130, 200)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    toolSizer->Add(MakeJumpBtn(toolPanel, "VRAM",  0x06000000, wxColour(100, 190, 220)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    toolSizer->Add(MakeJumpBtn(toolPanel, "ROM",   0x08000000, wxColour(180, 200, 160)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

    toolPanel->SetSizer(toolSizer);
    outerSizer->Add(toolPanel, 0, wxEXPAND);

    // Separator
    wxPanel* sep2 = new wxPanel(this);
    sep2->SetBackgroundColour(BORDER_COLOR);
    sep2->SetMinSize(wxSize(-1, 1));
    outerSizer->Add(sep2, 0, wxEXPAND);

    // ── Memory panel ─────────────────────────────────────────────────
    memoryPanel = new wxWindow(this, wxID_ANY);
    memoryPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    memoryPanel->SetBackgroundColour(BG_DARK);
    memoryPanel->Bind(wxEVT_PAINT,      &MemoryViewerFrame::OnPaint,      this);
    memoryPanel->Bind(wxEVT_MOUSEWHEEL, &MemoryViewerFrame::OnMouseWheel, this);
    memoryPanel->Bind(wxEVT_LEFT_DOWN,  &MemoryViewerFrame::OnLeftClick,  this);
    memoryPanel->Bind(wxEVT_SIZE,       &MemoryViewerFrame::OnMemPanelSize, this);
    outerSizer->Add(memoryPanel, 1, wxEXPAND);

    // ── Status bar ───────────────────────────────────────────────────
    CreateStatusBar(2);
    GetStatusBar()->SetBackgroundColour(BG_PANEL);
    SetStatusText("Ready", 0);
    SetStatusText("", 1);

    SetSizer(outerSizer);
    Layout();

    // Trigger initial size calculation
    wxSize initSize = memoryPanel->GetClientSize();
    RecalcVisibleRows(initSize.GetHeight());

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        e.Veto();
        Hide();
    });
}

void MemoryViewerFrame::RecalcVisibleRows(int panelHeight) {
    // Account for header row at top of memory panel
    int usable = panelHeight - headerHeight - 8;
    visibleRows = std::max(1, usable / rowHeight);
}

void MemoryViewerFrame::OnMemPanelSize(wxSizeEvent& event) {
    RecalcVisibleRows(event.GetSize().GetHeight());
    memoryPanel->Refresh();
    event.Skip();
}

void MemoryViewerFrame::OnGoto(wxCommandEvent&) {
    wxString text = gotoInput->GetValue().Trim();

    if (text.StartsWith("0x") || text.StartsWith("0X"))
        text = text.Mid(2);

    unsigned long address;
    if (text.ToULong(&address, 16)) {
        ScrollToAddress((uint32_t)address);
    } else {
        wxMessageBox("Invalid hexadecimal address", "Error", wxICON_ERROR);
    }
}

void MemoryViewerFrame::ScrollToAddress(uint32_t address) {
    viewOffset = address & ~0xF;
    gotoInput->SetValue(wxString::Format("0x%08X", viewOffset));
    memoryPanel->Refresh();
}

void MemoryViewerFrame::OnMouseWheel(wxMouseEvent& event) {
    int delta = event.GetWheelRotation();
    int lines = (delta / event.GetWheelDelta()) * 3;

    int64_t newOffset = (int64_t)viewOffset - (int64_t)(lines * bytesPerRow);
    newOffset = std::max<int64_t>(0, std::min<int64_t>(newOffset, 0x0FFFFFF0LL));
    viewOffset = (uint32_t)(newOffset & ~0xF);

    memoryPanel->Refresh();
}

void MemoryViewerFrame::OnLeftClick(wxMouseEvent& event) {
    // Calculate which address was clicked
    int clickY = event.GetY();
    int clickX = event.GetX();

    int relY = clickY - headerHeight;
    if (relY < 0) { event.Skip(); return; }

    int row = relY / rowHeight;
    // Hex columns start at xHex (120 px), each byte is 28px wide
    int xHex = 130;
    int col = (clickX - xHex) / 28;

    if (col >= 0 && col < bytesPerRow && row >= 0 && row < visibleRows) {
        selectedAddress = (int64_t)(viewOffset + row * bytesPerRow + col);
        memoryPanel->Refresh();

        uint8_t byte = memoryBus->read8((uint32_t)selectedAddress);
        SetStatusText(wxString::Format("0x%08X  =  0x%02X  (%u)  '%c'  [%s]",
            (uint32_t)selectedAddress, byte, byte,
            (byte >= 32 && byte < 127) ? (char)byte : '.',
            GetRegionName((uint32_t)selectedAddress)), 0);
    }
    event.Skip();
}

void MemoryViewerFrame::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(memoryPanel);
    RenderMemory(dc);
}

void MemoryViewerFrame::RenderMemory(wxDC& dc) {
    if (!memoryBus || !memoryPanel) return;

    dc.SetFont(monoFont);
    dc.SetBackground(wxBrush(BG_DARK));
    dc.Clear();

    wxSize sz = memoryPanel->GetClientSize();

    // Layout constants
    const int xRegion  = 6;
    const int xAddr    = 46;
    const int xHex     = 130;
    const int byteW    = 28;
    const int xAscii   = xHex + bytesPerRow * byteW + 12;

    // ── Column header ────────────────────────────────────────────────
    dc.SetBrush(wxBrush(HEADER_BG));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, sz.GetWidth(), headerHeight);

    // Bottom border of header
    dc.SetPen(wxPen(BORDER_COLOR, 1));
    dc.DrawLine(0, headerHeight - 1, sz.GetWidth(), headerHeight - 1);

    wxFont headerFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    dc.SetFont(headerFont);

    int hY = (headerHeight - dc.GetTextExtent("A").GetHeight()) / 2;

    dc.SetTextForeground(TEXT_DIM);
    dc.DrawText("RGN", xRegion, hY);
    dc.DrawText("ADDRESS", xAddr, hY);

    // Byte offset labels 00..0F
    for (int i = 0; i < bytesPerRow; i++) {
        // Highlight every 4th column header
        if (i % 4 == 0)
            dc.SetTextForeground(wxColour(130, 145, 175));
        else
            dc.SetTextForeground(TEXT_DIM);
        dc.DrawText(wxString::Format("%02X", i), xHex + i * byteW, hY);
    }

    dc.SetTextForeground(TEXT_DIM);
    dc.DrawText("ASCII", xAscii, hY);

    // ── Memory rows ──────────────────────────────────────────────────
    dc.SetFont(monoFont);
    uint32_t currentAddr = viewOffset;

    for (int row = 0; row < visibleRows; row++) {
        int rowY = headerHeight + row * rowHeight + 2;

        // Determine region color from first byte of row
        RegionColor rc = GetRegionColor(currentAddr);

        // Row background (alternating)
        wxColour rowBg = (row % 2 == 0) ? rc.bg : wxColour(
            std::min(255, rc.bg.Red()   + 6),
            std::min(255, rc.bg.Green() + 6),
            std::min(255, rc.bg.Blue()  + 6));
        dc.SetBrush(wxBrush(rowBg));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, rowY - 2, sz.GetWidth(), rowHeight);

        // Region tag
        dc.SetFont(wxFont(7, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(rc.text);
        dc.DrawText(GetRegionName(currentAddr), xRegion, rowY);
        dc.SetFont(monoFont);

        // Address
        dc.SetTextForeground(wxColour(150, 160, 180));
        dc.DrawText(wxString::Format("%08X", currentAddr), xAddr, rowY);

        // Bytes
        wxString ascii;
        for (int col = 0; col < bytesPerRow; col++) {
            uint32_t addr = currentAddr + col;
            uint8_t byte  = memoryBus->read8(addr);

            // Highlight selected cell
            if ((int64_t)addr == selectedAddress) {
                dc.SetBrush(wxBrush(ACCENT_BLUE));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(xHex + col * byteW - 2, rowY - 2, byteW, rowHeight);
                dc.SetTextForeground(*wxWHITE);
            } else {
                // Dim zero bytes
                if (byte == 0)
                    dc.SetTextForeground(TEXT_DIM);
                else
                    dc.SetTextForeground(rc.text);
            }

            // Column separator every 4 bytes (subtle)
            if (col > 0 && col % 4 == 0) {
                dc.SetPen(wxPen(wxColour(50, 55, 68), 1));
                dc.DrawLine(xHex + col * byteW - 4, rowY - 1,
                            xHex + col * byteW - 4, rowY + rowHeight - 3);
            }

            dc.DrawText(wxString::Format("%02X", byte), xHex + col * byteW, rowY);

            ascii += (byte >= 32 && byte < 127) ? (wchar_t)byte : L'.';
        }

        // ASCII
        dc.SetTextForeground(TEXT_DIM);
        dc.DrawText(ascii, xAscii, rowY);

        currentAddr += bytesPerRow;
        if (currentAddr > 0x0FFFFFFF) break;
    }

    // Update status with current view range
    SetStatusText(wxString::Format("0x%08X – 0x%08X  [%s]",
        viewOffset, currentAddr - 1, GetRegionName(viewOffset)), 1);
}

void MemoryViewerFrame::UpdateDisplay() {
    if (memoryPanel) memoryPanel->Refresh();
}

void MemoryViewerFrame::OnClose(wxCloseEvent& event) {
    event.Veto();
    Hide();
}