#pragma once
#include <cstdint>

/*Mode bits. These indicate the current processor mode:

10000 - User mode
10001 - FIQ mode
10010 - IRQ mode
10011 - Supervisor mode
10111 - Abort mode
11011 - Undefined mode
11111 - System mode*/

enum CPUMode : uint8_t
{
    User = 0x10,
    FIQ = 0x11,
    IRQ = 0x12,
    Supervisor = 0x13,
    Abort = 0x17,
    Undefined = 0x1B,
    System = 0x1F
};

//https://gbadev.net/gbadoc/cpu.html
struct ProgramStatusRegister
{
    uint32_t &CPSR;
    CPUMode GetMode() const { return (CPUMode)(CPSR & 0x1F); }
    void SetMode(CPUMode mode)
    {
        CPSR = (CPSR & ~0x1F) | (static_cast<uint32_t>(mode & 0x1F));   
    }

    /*Thumb state indicator. If set, the CPU is in Thumb state.
     *Otherwise it operates in normal ARM state.
     *Software should never attempt to modify this bit itself.*/
    bool GetThumbState() const { return (CPSR >> 5) & 1; }
    void SetThumbState(bool thumb)
    {
        CPSR = (CPSR & ~(1u << 5)) | (uint32_t(thumb) << 5);
    }

    //FIQ interrupt disable. Set this to disable FIQ interrupts.
    bool GetFIQDisable() const { return (CPSR >> 6) & 1; }
    void SetFIQDisable(bool disable)
    {
        CPSR = (CPSR & ~(1u << 6)) | (uint32_t(disable) << 6);
    }
    
    /*IRQ interrupt disable. Set this to disable IRQ interrupts.
     *On the GBA this is set by default whenever IRQ mode is entered.
     *Why or how this is the case, I do not know.*/
    bool GetIRQDisable() const { return (CPSR >> 7) & 1; }
    void SetIRQDisable(bool disable)
    {
        CPSR = (CPSR & ~(1u << 7)) | (uint32_t(disable) << 7);
    }

    //Overflow condition code
    bool GetOverflow() const { return (CPSR >> 28) & 1; }
    void SetOverflow(bool overflow)
    {
        CPSR = (CPSR & ~(1u << 28)) | (uint32_t(overflow) << 28);
    }
    
    //Carry/Borrow/Extend condition code
    bool GetCarry() const { return (CPSR >> 29) & 1; }
    void SetCarry(bool carry)
    {
        CPSR = (CPSR & ~(1u << 29)) | (uint32_t(carry) << 29);
    }
    
    //Zero/Equal condition code
    bool GetZero() const { return (CPSR >> 30) & 1; }
    void SetZero(bool zero)
    {
        CPSR = (CPSR & ~(1u << 30)) | (uint32_t(zero) << 30);
    }
    
    //Negative/Less than condition code
    bool GetNegative() const { return (CPSR >> 31) & 1; }
    void SetNegative(bool negative)
    {
        CPSR = (CPSR & ~(1u << 31)) | (uint32_t(negative) << 31);
    }
};

class ARMRegisters
{
private:
    //general registers
    uint32_t R0;
    uint32_t R1;
    uint32_t R2;
    uint32_t R3;
    uint32_t R4;
    uint32_t R5;
    uint32_t R6;
    uint32_t R7;
    uint32_t R8;
    uint32_t R9;
    uint32_t R10;
    uint32_t R11;
    uint32_t R12;

    //special registers
    uint32_t StackPointer; //r13
    uint32_t LinkRegister; //r14
    uint32_t ProgramCounter; //r15
    uint32_t CPSR; //r16 - current program status register

    //irq registers - these are named wrong, but i already use them... so like... does it matter that much?
    uint32_t R13_irq;
    uint32_t R14_irq;
    uint32_t SPSR_irq;

    //fiq registers
    uint32_t R8_fiq;
    uint32_t R9_fiq;
    uint32_t R10_fiq;
    uint32_t R11_fiq;
    uint32_t R12_fiq;
    uint32_t StackPointer_fiq;
    uint32_t LinkRegister_fiq;
    uint32_t SPSR_fiq;

    //svc registers
    uint32_t StackPointer_svc;
    uint32_t LinkRegister_svc;
    uint32_t SPSR_svc;

    //abt registers
    uint32_t StackPointer_abt;
    uint32_t LinkRegister_abt;
    uint32_t SPSR_abt;

    //und registers
    uint32_t StackPointer_und;
    uint32_t LinkRegister_und;
    uint32_t SPSR_und;

    //for display purposes
    uint32_t EmptySPSR;

public:
    ProgramStatusRegister GetProgramStatusRegister();
    ProgramStatusRegister GetSavedProgramStatusRegister();
    uint32_t* GetRegister(uint8_t Register);
};
