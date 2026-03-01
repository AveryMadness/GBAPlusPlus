// RegisterFrame.cpp
#include "RegisterFrame.h"
#include <sstream>
#include <iomanip>

// Dark theme colors
static const wxColour BG_DARK       (18,  20,  24);
static const wxColour BG_PANEL      (26,  29,  36);
static const wxColour BG_ROW_ALT    (32,  36,  44);
static const wxColour ACCENT_BLUE   (82,  139, 255);
static const wxColour ACCENT_GREEN  (80,  200, 120);
static const wxColour ACCENT_ORANGE (255, 160,  60);
static const wxColour ACCENT_RED    (255,  80,  80);
static const wxColour TEXT_PRIMARY  (220, 225, 235);
static const wxColour TEXT_DIM      (100, 110, 130);
static const wxColour BORDER_COLOR  (45,  50,  62);

static const char* ModeToString(CPUMode mode) {
    switch (mode) {
    case User:       return "USR";
    case FIQ:        return "FIQ";
    case IRQ:        return "IRQ";
    case Supervisor: return "SVC";
    case Abort:      return "ABT";
    case Undefined:  return "UND";
    case System:     return "SYS";
    default:         return "???";
    }
}

RegisterFrame::RegisterFrame(wxWindow* parent, ARMRegisters* regs)
    : wxFrame(parent, wxID_ANY, "Registers",
              wxDefaultPosition, wxSize(340, 640),
              wxDEFAULT_FRAME_STYLE & ~wxRESIZE_BORDER & ~wxMAXIMIZE_BOX)
    , registers(regs)
{
    SetBackgroundColour(BG_DARK);
    SetForegroundColour(TEXT_PRIMARY);

    wxFont monoFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
#ifdef __WXMSW__
    monoFont = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                      false, "Cascadia Mono");
    if (!monoFont.IsOk())
        monoFont = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
#endif

    wxBoxSizer* outerSizer = new wxBoxSizer(wxVERTICAL);

    // Title bar strip
    wxPanel* titlePanel = new wxPanel(this);
    titlePanel->SetBackgroundColour(BG_PANEL);
    titlePanel->SetMinSize(wxSize(-1, 36));
    wxBoxSizer* titleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* titleLabel = new wxStaticText(titlePanel, wxID_ANY, " \u2261  REGISTERS");
    wxFont titleFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    titleLabel->SetFont(titleFont);
    titleLabel->SetForegroundColour(ACCENT_BLUE);
    titleLabel->SetBackgroundColour(BG_PANEL);
    titleSizer->Add(titleLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    titlePanel->SetSizer(titleSizer);
    outerSizer->Add(titlePanel, 0, wxEXPAND);

    // Separator line
    wxPanel* sep = new wxPanel(this);
    sep->SetBackgroundColour(BORDER_COLOR);
    sep->SetMinSize(wxSize(-1, 1));
    outerSizer->Add(sep, 0, wxEXPAND);

    // Custom-drawn register panel
    registerPanel = new wxPanel(this, wxID_ANY);
    registerPanel->SetBackgroundColour(BG_DARK);
    registerPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    registerPanel->Bind(wxEVT_PAINT, &RegisterFrame::OnPaint, this);
    outerSizer->Add(registerPanel, 1, wxEXPAND | wxALL, 0);

    SetSizer(outerSizer);
    Layout();

    // Bind close to hide
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        e.Veto();
        Hide();
    });
}

void RegisterFrame::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(registerPanel);
    DrawRegisters(dc);
}

void RegisterFrame::DrawRegisters(wxDC& dc) {
    if (!registers) return;

    wxFont monoFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    wxFont labelFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    wxFont flagFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);

    dc.SetBackground(wxBrush(BG_DARK));
    dc.Clear();

    wxSize sz = registerPanel->GetClientSize();
    int rowH = 22;
    int y = 10;
    int xLabel = 18;
    int xValue = 80;
    int xBar   = 195;

    // Helper: draw one register row
    auto DrawRow = [&](int row, const wxString& name, uint32_t value, wxColour nameColor) {
        // Alternating row background
        if (row % 2 == 0) {
            dc.SetBrush(wxBrush(BG_ROW_ALT));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(8, y - 3, sz.GetWidth() - 16, rowH);
        }

        // Register name
        dc.SetFont(labelFont);
        dc.SetTextForeground(nameColor);
        dc.DrawText(name, xLabel, y);

        // Hex value
        dc.SetFont(monoFont);
        dc.SetTextForeground(TEXT_PRIMARY);
        wxString hexVal = wxString::Format("0x%08X", value);
        dc.DrawText(hexVal, xValue, y);

        // Decimal (dim)
        dc.SetTextForeground(TEXT_DIM);
        wxString decVal = wxString::Format("%u", value);
        dc.DrawText(decVal, xBar, y);

        y += rowH;
    };

    // General purpose registers R0-R12
    static const char* gprNames[] = {
        "R0","R1","R2","R3","R4","R5","R6",
        "R7","R8","R9","R10","R11","R12"
    };
    for (int i = 0; i <= 12; i++) {
        DrawRow(i, gprNames[i], *registers->GetRegister(i), ACCENT_BLUE);
    }

    // Separator
    dc.SetPen(wxPen(BORDER_COLOR, 1));
    dc.DrawLine(8, y, sz.GetWidth() - 8, y);
    y += 6;

    // Special registers
    DrawRow(13, "SP",  *registers->GetRegister(STACK_POINTER),   ACCENT_ORANGE);
    DrawRow(14, "LR",  *registers->GetRegister(LINK_REGISTER),   ACCENT_ORANGE);
    DrawRow(15, "PC",  *registers->GetRegister(PROGRAM_COUNTER), ACCENT_GREEN);

    // Separator
    dc.SetPen(wxPen(BORDER_COLOR, 1));
    dc.DrawLine(8, y, sz.GetWidth() - 8, y);
    y += 8;

    // PSR section header
    dc.SetFont(labelFont);
    dc.SetTextForeground(TEXT_DIM);
    dc.DrawText("STATUS REGISTERS", xLabel, y);
    y += rowH;

    ProgramStatusRegister cpsr = registers->GetProgramStatusRegister();
    ProgramStatusRegister spsr = registers->GetSavedProgramStatusRegister();

    DrawRow(0, "CPSR", cpsr.CPSR, ACCENT_ORANGE);
    DrawRow(1, "SPSR", spsr.CPSR, ACCENT_ORANGE);

    // Mode badge
    y += 4;
    wxString modeName = wxString::Format("MODE: %s", ModeToString(cpsr.GetMode()));
    wxString thumbStr = cpsr.GetThumbState() ? "THUMB" : "ARM";
    dc.SetFont(labelFont);
    dc.SetTextForeground(ACCENT_GREEN);

    // Mode pill background
    dc.SetBrush(wxBrush(wxColour(30, 50, 30)));
    dc.SetPen(wxPen(ACCENT_GREEN, 1));
    wxSize modeTextSz = dc.GetTextExtent(modeName);
    dc.DrawRoundedRectangle(xLabel - 4, y - 2, modeTextSz.x + 12, rowH - 2, 4);
    dc.DrawText(modeName, xLabel + 2, y);

    // Thumb/ARM badge
    wxColour thumbColor = cpsr.GetThumbState() ? ACCENT_ORANGE : ACCENT_BLUE;
    dc.SetBrush(wxBrush(wxColour(40, 35, 20)));
    dc.SetPen(wxPen(thumbColor, 1));
    wxSize thumbSz = dc.GetTextExtent(thumbStr);
    int thumbX = xLabel + modeTextSz.x + 20;
    dc.DrawRoundedRectangle(thumbX - 4, y - 2, thumbSz.x + 12, rowH - 2, 4);
    dc.SetTextForeground(thumbColor);
    dc.DrawText(thumbStr, thumbX + 2, y);
    y += rowH + 8;

    // Flags row
    dc.SetFont(labelFont);
    dc.SetTextForeground(TEXT_DIM);
    dc.DrawText("FLAGS", xLabel, y);
    y += rowH;

    // Draw individual flag boxes
    struct FlagDef { const char* name; bool set; };
    FlagDef flags[] = {
        {"N", cpsr.GetNegative()},
        {"Z", cpsr.GetZero()},
        {"C", cpsr.GetCarry()},
        {"V", cpsr.GetOverflow()},
        {"I", cpsr.GetIRQDisable()},
        {"F", cpsr.GetFIQDisable()},
        {"T", cpsr.GetThumbState()},
    };

    int flagX = xLabel;
    int flagW = 32;
    dc.SetFont(flagFont);
    for (auto& f : flags) {
        wxColour fg = f.set ? ACCENT_GREEN : TEXT_DIM;
        wxColour bg = f.set ? wxColour(20, 45, 25) : wxColour(25, 27, 32);
        wxColour border = f.set ? ACCENT_GREEN : BORDER_COLOR;

        dc.SetBrush(wxBrush(bg));
        dc.SetPen(wxPen(border, 1));
        dc.DrawRoundedRectangle(flagX, y - 2, flagW, rowH + 2, 4);

        dc.SetTextForeground(fg);
        wxSize ts = dc.GetTextExtent(f.name);
        dc.DrawText(f.name, flagX + (flagW - ts.x) / 2, y);
        flagX += flagW + 5;
    }
}

void RegisterFrame::UpdateDisplay() {
    if (registerPanel) registerPanel->Refresh();
}