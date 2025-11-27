#include "RegisterFrame.h"
#include <sstream>
#include <iomanip>

RegisterFrame::RegisterFrame(wxWindow* parent, ARMRegisters* regs)
    : wxFrame(parent, wxID_ANY, "Registers", wxDefaultPosition, wxSize(400, 600))
    , registers(regs)
{
    wxFont font(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    
    textCtrl = new wxTextCtrl(
        this,
        wxID_ANY,
        "",
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP
    );
    textCtrl->SetFont(font);
    textCtrl->SetBackgroundColour(*wxWHITE);
    
    UpdateDisplay();
}

static const char* ModeToString(CPUMode mode) {
    switch (mode) {
    case User: return "User";
    case FIQ: return "FIQ";
    case IRQ: return "IRQ";
    case Supervisor: return "Supervisor";
    case Abort: return "Abort";
    case Undefined: return "Undefined";
    case System: return "System";
    default: return "Unknown";
    }
}
void RegisterFrame::UpdateDisplay() {
    if (!registers) return;
    
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    
    // General-purpose registers
    for (int i = 0; i <= 12; ++i) {
        uint32_t* reg = registers->GetRegister(i);
        oss << "R" << std::dec << i << ":  0x" << std::hex 
            << std::setw(8) << std::setfill('0') << *reg << "\n";
    }
    
    // Special registers
    oss << "SP:  0x" << std::setw(8) << std::setfill('0') 
        << *registers->GetRegister(STACK_POINTER) << "\n";
    oss << "LR:  0x" << std::setw(8) << std::setfill('0') 
        << *registers->GetRegister(LINK_REGISTER) << "\n";
    oss << "PC:  0x" << std::setw(8) << std::setfill('0') 
        << *registers->GetRegister(PROGRAM_COUNTER) << "\n";
    
    // CPSR
    ProgramStatusRegister psr = registers->GetProgramStatusRegister();
    
    oss << "\nMode: " << ModeToString(psr.GetMode()) << "\n";
    oss << "CPSR: 0x" << std::setw(8) << std::setfill('0') << psr.CPSR << "\n";
    
    ProgramStatusRegister spsr = registers->GetSavedProgramStatusRegister();
    oss << "SPSR: 0x" << std::setw(8) << std::setfill('0') << spsr.CPSR << "\n";
    
    // Flags
    oss << "\nFlags: ";
    oss << (psr.GetNegative() ? "N" : "-") << " ";
    oss << (psr.GetZero() ? "Z" : "-") << " ";
    oss << (psr.GetCarry() ? "C" : "-") << " ";
    oss << (psr.GetOverflow() ? "V" : "-") << " ";
    oss << (psr.GetIRQDisable() ? "I" : "-") << " ";
    oss << (psr.GetFIQDisable() ? "F" : "-") << " ";
    oss << (psr.GetThumbState() ? "T" : "-") << "\n";
    
    textCtrl->SetValue(wxString::FromUTF8(oss.str()));
}