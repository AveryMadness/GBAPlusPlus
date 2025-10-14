#include "ARMRegisters.h"

#include <exception>

uint32_t* ARMRegisters::GetRegister(uint8_t Register)
{
    CPUMode mode = GetProgramStatusRegister().GetMode();
    switch (Register)
    {
    case 0:
        return &R0;
    case 1:
        return &R1;
    case 2:
        return &R2;
    case 3:
        return &R3;
    case 4:
        return &R4;
    case 5:
        return &R5;
    case 6:
        return &R6;
    case 7:
        return &R7;
    case 8:
        {
            if (mode == FIQ)
            {
                return &R8_fiq;
            }

            return &R8;
        }
    case 9:
        {
            if (mode == FIQ)
            {
                return &R9_fiq;
            }

            return &R9;
        }
    case 10:
        {
            if (mode == FIQ)
            {
                return &R10_fiq;
            }

            return &R10;
        }
    case 11:
        {
            if (mode == FIQ)
            {
                return &R11_fiq;
            }

            return &R11;
        }
    case 12:
        {
            if (mode == FIQ)
            {
                return &R12_fiq;
            }

            return &R12;
        }
    case 13:
        {
            switch (mode)
            {
            case FIQ:
                return &StackPointer_fiq;
            case IRQ:
                return &R13_irq;
            case Supervisor:
                return &StackPointer_svc;
            case Abort:
                return &StackPointer_abt;
            case Undefined:
                return &StackPointer_und;
            default:
                return &StackPointer;
            }
        }
    case 14:
        {
            switch (mode)
            {
            case FIQ:
                return &LinkRegister_fiq;
            case IRQ:
                return &R14_irq;
            case Supervisor:
                return &LinkRegister_svc;
            case Abort:
                return &LinkRegister_abt;
            case Undefined:
                return &LinkRegister_und;
            default:
                return &LinkRegister;
            }
        }
    case 15:
        return &ProgramCounter;
    default:
        throw std::exception("Register is not a valid register.");
    }
}

ProgramStatusRegister ARMRegisters::GetProgramStatusRegister()
{
    return ProgramStatusRegister{CPSR};
}

ProgramStatusRegister ARMRegisters::GetSavedProgramStatusRegister()
{
    ProgramStatusRegister StatusReg = GetProgramStatusRegister();
    CPUMode Mode = StatusReg.GetMode();

    switch (Mode)
    {
    case IRQ:
        return ProgramStatusRegister{SPSR_irq};
    case FIQ:
        return ProgramStatusRegister{SPSR_fiq};
    case Supervisor:
        return ProgramStatusRegister{SPSR_svc};
    case Abort:
        return ProgramStatusRegister{SPSR_abt};
    case Undefined:
        return ProgramStatusRegister{SPSR_und};
    default:
        return ProgramStatusRegister{EmptySPSR};
    }
}

