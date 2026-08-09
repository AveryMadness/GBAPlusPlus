#include "ARM7TDMI.h"

#include <iostream>
#include <string>
#include <Windows.h>
#include "Disassembler.h"

ARM7TDMI::ARM7TDMI(MemoryBus* memoryBus, ARMRegisters* registers)
{
    this->memoryBus = memoryBus;
    this->registers = registers;

    buildArmTable();
    buildThumbTable();
}

void ARM7TDMI::InitializeCpuForExecution()
{
    flushPipeline();
}

void ARM7TDMI::executeARMInstruction(uint32_t instruction)
{
    ArmInstruction function = determineArmInstruction(instruction);
    (this->*function)(instruction);
}

void ARM7TDMI::executeThumbInstruction(uint16_t instruction)
{
    ThumbInstruction function = determineThumbInstruction(instruction);
    (this->*function)(instruction);
}

void ARM7TDMI::runCpuStep()
{
    if (memoryBus->IsHalted())
    {
        //keep running hardware
        memoryBus->TickPPU();
        memoryBus->TickTimers();
        totalCycles++;
        
        if (InterruptWaiting())
        {
            memoryBus->ClearHalt();

            if (InterruptPending())
                EnterInterrupt();
        }
        return;
    }

    bool thumbMode = registers->GetProgramStatusRegister().GetThumbState();
    //trace stuff
    bool tracingThisStep = static_cast<bool>(traceFile);
    uint32_t traceAddress = 0;
    uint32_t traceOpcode = 0;
    if (tracingThisStep)
    {
        uint32_t pcNow = *registers->GetRegister(PROGRAM_COUNTER);
        traceAddress = thumbMode ? (pcNow - 4) : (pcNow - 8);
        traceOpcode = thumbMode ? ThumbExecutingInstruction : ExecutingInstruction;
    }

    bool conditionPassed = true;

    try
    {
        if (!thumbMode)
        {
            //arm mode
            ConditionCode condition = static_cast<ConditionCode>((ExecutingInstruction >> 28) & 0xF);
            conditionPassed = checkCondition(condition);

            if (conditionPassed)
            {
                //execute instruction ready to be executed
                executeARMInstruction(ExecutingInstruction);
            }

            //don't move up instructions after flushing pipeline.
            if (isFlushed)
            {
                isFlushed = false;
            }
            else
            {
                //move up the decoding instruction
                ExecutingInstruction = DecodingInstruction;
                //move up the fetched instruction
                DecodingInstruction = Read32();
            }
        }
        else
        {
            executeThumbInstruction(ThumbExecutingInstruction);

            if (isFlushed)
            {
                isFlushed = false;
            }
            else
            {
                ThumbExecutingInstruction = ThumbDecodingInstruction;
                ThumbDecodingInstruction = Read16();
            }
        }
    }
    catch (...)
    {
        if (tracingThisStep)
            WriteTraceLine(traceAddress, thumbMode, traceOpcode, conditionPassed, true);
        throw;
    }

    if (tracingThisStep)
        WriteTraceLine(traceAddress, thumbMode, traceOpcode, conditionPassed, false);

    //advance timers
    uint32_t elapsedCycles = memoryBus->ConsumeCycles();
    for (uint32_t i = 0; i < elapsedCycles; i++)
    {
        memoryBus->TickPPU();
        memoryBus->TickTimers();
    }
    totalCycles += elapsedCycles;

    if (InterruptPending())
        EnterInterrupt();
}

uint64_t ARM7TDMI::GetTotalCycles() const
{
    return totalCycles;
}

bool ARM7TDMI::EnableTracing(const std::string& filePath, size_t maxLines)
{
    auto file = std::make_unique<std::ofstream>(filePath, std::ios::out | std::ios::trunc);
    if (!file->is_open())
        return false;

    traceFile = std::move(file);
    traceLineCount = 0;
    traceMaxLines = maxLines;
    traceHasPrevious = false;
    traceLastFlags.clear();

    *traceFile << "; address   mode  opcode    disassembly (up to first divergence is what matters)\n";
    *traceFile << "; followed by post-instruction register state: R0-R12 SP LR PC CPSR[NZCVIFT] MODE\n";
    *traceFile << "; only registers that changed since the previous line are shown\n";
    return true;
}

void ARM7TDMI::DisableTracing()
{
    traceFile.reset();
}

bool ARM7TDMI::IsTracing() const
{
    return static_cast<bool>(traceFile);
}

void ARM7TDMI::WriteTraceLine(uint32_t address, bool thumbMode, uint32_t opcode, bool conditionPassed, bool threw)
{
    if (!traceFile || !traceFile->is_open())
        return;

    char addrBuf[9];
    snprintf(addrBuf, sizeof(addrBuf), "%08X", address);

    char opBuf[9];
    if (thumbMode)
        snprintf(opBuf, sizeof(opBuf), "%04X    ", static_cast<uint16_t>(opcode));
    else
        snprintf(opBuf, sizeof(opBuf), "%08X", opcode);

    std::string disasm = thumbMode
        ? Disassembler::DisassembleThumb(static_cast<uint16_t>(opcode), address)
        : Disassembler::DisassembleARM(opcode, address);

    *traceFile << addrBuf << " " << (thumbMode ? "THUMB" : "ARM  ") << " " << opBuf << "  "
               << (conditionPassed ? "" : "(skip) ") << disasm;

    if (threw)
    {
        *traceFile << "   <-- THREW (exception below)\n";
        traceFile->flush();
        traceLineCount++;
        return;
    }

    static const char* regNames[16] = {
        "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
        "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC"
    };

    uint32_t currentRegs[16];
    for (int i = 0; i <= 12; i++)
        currentRegs[i] = *registers->GetRegister(static_cast<uint8_t>(i));
    currentRegs[13] = *registers->GetRegister(STACK_POINTER);
    currentRegs[14] = *registers->GetRegister(LINK_REGISTER);
    currentRegs[15] = *registers->GetRegister(PROGRAM_COUNTER);

    char regBuf[16];
    for (int i = 0; i < 16; i++)
    {
        if (!traceHasPrevious || currentRegs[i] != traceLastRegs[i])
        {
            snprintf(regBuf, sizeof(regBuf), " %s=%08X", regNames[i], currentRegs[i]);
            *traceFile << regBuf;
        }
        traceLastRegs[i] = currentRegs[i];
    }

    ProgramStatusRegister psr = registers->GetProgramStatusRegister();
    char flagsBuf[8];
    snprintf(flagsBuf, sizeof(flagsBuf), "%c%c%c%c%c%c%c",
        psr.GetNegative() ? 'N' : '-',
        psr.GetZero() ? 'Z' : '-',
        psr.GetCarry() ? 'C' : '-',
        psr.GetOverflow() ? 'V' : '-',
        psr.GetIRQDisable() ? 'I' : '-',
        psr.GetFIQDisable() ? 'F' : '-',
        psr.GetThumbState() ? 'T' : '-');
    std::string modeStr = Disassembler::ModeToString(psr.GetMode());
    std::string flagsAndMode = std::string(flagsBuf) + " " + modeStr;

    if (!traceHasPrevious || flagsAndMode != traceLastFlags)
        *traceFile << " CPSR=" << flagsBuf << " MODE=" << modeStr;
    *traceFile << "\n";

    traceLastFlags = flagsAndMode;
    traceHasPrevious = true;

    traceLineCount++;
    if (traceMaxLines > 0 && traceLineCount >= traceMaxLines)
    {
        *traceFile << "; trace line cap (" << traceMaxLines << ") reached, tracing auto-stopped\n";
        DisableTracing();
        return;
    }

    if ((traceLineCount & 0xFF) == 0)
        traceFile->flush();
}

uint32_t ARM7TDMI::Read32()
{
    uint32_t* programCounter = registers->GetRegister(PROGRAM_COUNTER);
    uint32_t value =  memoryBus->read32(*programCounter);

    *registers->GetRegister(PROGRAM_COUNTER) += 4;

    return value;
}

uint16_t ARM7TDMI::Read16()
{
    uint32_t* programCounter = registers->GetRegister(PROGRAM_COUNTER);
    uint16_t value = memoryBus->read16(*programCounter);

    *registers->GetRegister(PROGRAM_COUNTER) += 2;

    return value;
}

uint32_t ARM7TDMI::LoadWord(uint32_t address)
{
    uint32_t word = memoryBus->read32(address & ~0x3);
    uint8_t alignment = address & 0x3;
    uint32_t rotateAmount = alignment * 8;

    return (word >> rotateAmount) | (word << (32 - rotateAmount));
}

void ARM7TDMI::StoreWord(uint32_t address, uint32_t value)
{
    uint8_t alignment = address & 0x3;
    uint32_t rotateAmount = alignment * 8;

    uint32_t rotatedValue = (value << rotateAmount) | (value >> (32 - rotateAmount));

    memoryBus->write32(address & ~0x3, rotatedValue);
}

void ARM7TDMI::buildArmTable()
{
    //init all table entries to undefined, cant have any empty functions!
    for (int i = 0; i < 4096; i++)
    {
        armTable[i] = &ARM7TDMI::armUndefined;
    }

    for (int i = 0; i < 4096; i++)
    {
        uint32_t bits27_20 = (i >> 4) & 0xFF;
        uint32_t bits7_4 = i & 0xF;

        //instruction pattern, can be calculated per instruction to figure out what instruction to run
        //uint32_t pattern (bits27_20 << 20 | (bits7_4 << 4));

        //i have absolutely zero clue how i would describe what this code does
        //https://iitd-plos.github.io/col718/ref/arm-instructionset.pdf
        if ((bits27_20 & 0xC0) == 0x00)
        {
            if ((bits27_20 & 0xFC) == 0x00 && bits7_4 == 0x9)
            {
                armTable[i] = &ARM7TDMI::armMultiply;
            }
            else if ((bits27_20 & 0xF8) == 0x08 && bits7_4 == 0x09)
            {
                armTable[i] = &ARM7TDMI::armMultiplyLong;
            }
            else if ((bits27_20 & 0xFB) == 0x10 && bits7_4 == 0x9)
            {
                armTable[i] = &ARM7TDMI::armSingleDataSwap;
            }
            else if (bits27_20 == 0x12 && bits7_4 == 0x01)
            {
                armTable[i] = &ARM7TDMI::armBranchExchange;
            }
            else if ((bits7_4 & 0x9) == 0x9 && (bits27_20 & 0xE0) == 0x00)
            {
                armTable[i] = &ARM7TDMI::armHalfwordDataTransfer;
            }
            else if ((bits27_20 & 0xFB) == 0x10 && bits7_4 == 0x0)
            {
                armTable[i] = &ARM7TDMI::armPSRTransfer;
            }
            else if ((bits27_20 & 0xFB) == 0x12 && bits7_4 == 0x0)
            {
                armTable[i] = &ARM7TDMI::armPSRTransfer;
            }
            else if ((bits27_20 & 0xFB) == 0x32)
            {
                armTable[i] = &ARM7TDMI::armPSRTransfer;
            }
            else
            {
                armTable[i] = &ARM7TDMI::armDataProcessing;
            }
        }
        else if ((bits27_20 & 0xC0) == 0x40)
        {
            armTable[i] = &ARM7TDMI::armSingleDataTransfer;
        }
        else if ((bits27_20 & 0xE0) == 0x80)
        {
            armTable[i] = &ARM7TDMI::armBlockDataTransfer;
        }
        else if ((bits27_20 & 0xE0) == 0xA0)
        {
            armTable[i] = &ARM7TDMI::armBranch;
        }
        else if ((bits27_20 & 0xE0) == 0xC0)
        {
            armTable[i] = &ARM7TDMI::armCoprocessorRegisterTransfer;
        }
        else if ((bits27_20 & 0xF0) == 0xE0 && (bits7_4 & 0x1) == 0x0)
        {
            armTable[i] = &ARM7TDMI::armCoprocessorDataOperation;
        }
        else if ((bits27_20 & 0xF0) == 0xE0 && (bits7_4 & 0x1) == 0x1)
        {
            armTable[i] = &ARM7TDMI::armCoprocessorRegisterTransfer;
        }
        else if ((bits27_20 & 0xF0) == 0xF0)
        {
            armTable[i] = &ARM7TDMI::armSoftwareInterrupt;
        }
    }
}

void ARM7TDMI::buildThumbTable()
{
    for (int i = 0; i < 1024; i++)
    {
        thumbTable[i] = &ARM7TDMI::thumbUndefined;
    }

    for (int i = 0; i < 1024; i++)
    {
        uint32_t bits15_13 = (i >> 7) & 0x7;
        uint32_t bits12_11 = (i >> 5) & 0x3;
        uint32_t bits10_8  = (i >> 2) & 0x7;
        uint32_t bits15_10 = (i >> 4) & 0x3F;

        // Move Shifted Register: bits15-13=000, bits12-11 != 11 (LSL/LSR/ASR)
        if (bits15_13 == 0b000 && bits12_11 != 0b11)
        {
            thumbTable[i] = &ARM7TDMI::thumbMoveShiftedRegister;
        }
        // Add/Subtract: bits15-11=00011
        else if (bits15_13 == 0b000 && bits12_11 == 0b11)
        {
            thumbTable[i] = &ARM7TDMI::thumbAddSubtract;
        }
        // Move/Compare/Add/Subtract Immediate: bits15-13=001
        else if (bits15_13 == 0b001)
        {
            thumbTable[i] = &ARM7TDMI::thumbMoveCompareAddSubtractImmediate;
        }
        // ALU Operations: bits15-10=010000
        else if (bits15_10 == 0b010000)
        {
            thumbTable[i] = &ARM7TDMI::thumbALUOperations;
        }
        // Hi Register Operations / BX: bits15-10=010001
        else if (bits15_10 == 0b010001)
        {
            thumbTable[i] = &ARM7TDMI::thumbHiRegisterOperations;
        }
        // PC-Relative Load: bits15-11=01001
        else if (bits15_13 == 0b010 && bits12_11 == 0b01)
        {
            thumbTable[i] = &ARM7TDMI::thumbPCRelativeLoad;
        }
        // Load/Store Register Offset vs Sign-Extended:
        // Both have bits15-12=0101. Distinguished by instruction bit 9 (index bit 3 of bits10_8).
        //   bit9=0 -> register offset (STR/STRB/LDR/LDRB)
        //   bit9=1 -> sign-extended   (STRH/LDSB/LDRH/LDSH)
        else if (bits15_13 == 0b010 && bits12_11 == 0b10 && (bits10_8 & 0b010) == 0)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreRegisterOffset;
        }
        else if (bits15_13 == 0b010 && bits12_11 == 0b10 && (bits10_8 & 0b010) != 0)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreSignExtended;
        }
        else if (bits15_13 == 0b010 && bits12_11 == 0b11 && (bits10_8 & 0b010) == 0)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreRegisterOffset;
        }
        else if (bits15_13 == 0b010 && bits12_11 == 0b11 && (bits10_8 & 0b010) != 0)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreSignExtended;
        }
        // Load/Store Immediate Offset: bits15-13=011
        else if (bits15_13 == 0b011)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreImmediateOffset;
        }
        // Load/Store Halfword: bits15-13=100, bits12-11=00 or 01
        else if (bits15_13 == 0b100 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreHalfword;
        }
        // SP-Relative Load/Store: bits15-13=100, bits12-11=10 or 11
        else if (bits15_13 == 0b100 && (bits12_11 == 0b10 || bits12_11 == 0b11))
        {
            thumbTable[i] = &ARM7TDMI::thumbSPRelativeLoadStore;
        }
        // Load Address (ADD Rd, PC/SP): bits15-13=101, bits12-11=00 or 01
        else if (bits15_13 == 0b101 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadAddress;
        }
        // Add Offset to SP: bits15-8=10110000/10110001 -> bits15_13=101, bits12_11=10, bits10_8=000
        else if (bits15_13 == 0b101 && bits12_11 == 0b10 && bits10_8 == 0b000)
        {
            thumbTable[i] = &ARM7TDMI::thumbAddOffsetToSP;
        }
        // Push/Pop: bits15_13=101, bits12_11=10 or 11, bits10_8=100 or 101
        else if (bits15_13 == 0b101 && (bits12_11 == 0b10 || bits12_11 == 0b11)
                 && (bits10_8 == 0b100 || bits10_8 == 0b101))
        {
            thumbTable[i] = &ARM7TDMI::thumbPushPopRegisters;
        }
        // Multiple Load/Store (LDMIA/STMIA): bits15-13=110, bits12-11=00 or 01
        else if (bits15_13 == 0b110 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        {
            thumbTable[i] = &ARM7TDMI::thumbMultipleLoadStore;
        }
        // SWI: bits15-8=11011111 -> bits15_13=110, bits12_11=11, bits10_8=111
        else if (bits15_13 == 0b110 && bits12_11 == 0b11 && bits10_8 == 0b111)
        {
            thumbTable[i] = &ARM7TDMI::thumbSoftwareInterrupt;
        }
        // Conditional Branch: bits15-12=1101, condition 0x0-0xE
        // bits12_11=10 (cond 0x0-0x7) or bits12_11=11 with bits10_8 != 111 (cond 0x8-0xE)
        else if (bits15_13 == 0b110 && (bits12_11 == 0b10 || bits12_11 == 0b11))
        {
            thumbTable[i] = &ARM7TDMI::thumbConditionalBranch;
        }
        // Unconditional Branch: bits15-13=111, bits12_11=00 or 01
        else if (bits15_13 == 0b111 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        {
            thumbTable[i] = &ARM7TDMI::thumbUnconditionalBranch;
        }
        // Long Branch with Link: bits15-11=11110 (hi half) or 11111 (lo half)
        else if (bits15_13 == 0b111 && (bits12_11 == 0b10 || bits12_11 == 0b11))
        {
            thumbTable[i] = &ARM7TDMI::thumbLongBranchWithLink;
        }
    }
}

bool ARM7TDMI::checkCondition(ConditionCode condition)
{
    ProgramStatusRegister cpsr = registers->GetProgramStatusRegister();
    switch (condition)
    {
    case Always:
        return true;
    case Never:
        return false;
    case Equal:
        return cpsr.GetZero();
    case NotEqual:
        return !cpsr.GetZero();
    case CarrySet:
        return cpsr.GetCarry();
    case CarryClear:
        return !cpsr.GetCarry();
    case Minus:
        return cpsr.GetNegative();
    case Plus:
        return !cpsr.GetNegative();
    case Overflow:
        return cpsr.GetOverflow();
    case NoOverflow:
        return !cpsr.GetOverflow();
    case UnsignedHigher:
        return cpsr.GetCarry() && !cpsr.GetZero();
    case UnsignedLowerOrSame:
        return !cpsr.GetCarry() || cpsr.GetZero();
    case SignedGreaterOrSame:
        return cpsr.GetNegative() == cpsr.GetOverflow();
    case SignedLessThan:
        return cpsr.GetNegative() != cpsr.GetOverflow();
    case SignedGreaterThan:
        return cpsr.GetNegative() == cpsr.GetOverflow() && !cpsr.GetZero();
    case SignedLessThanOrEqual:
        return cpsr.GetZero() || cpsr.GetNegative() != cpsr.GetOverflow();
    }

    return false;
}

ARM7TDMI::ArmInstruction ARM7TDMI::determineArmInstruction(uint32_t instruction)
{
    uint32_t bits27_20 = (instruction >> 20) & 0xFF;
    uint32_t bits7_4 = (instruction >> 4) & 0xF;

    //instruction pattern, index in table for instruction function
    uint32_t pattern = bits27_20 << 4 | bits7_4;

    return armTable[pattern];
}

ARM7TDMI::ThumbInstruction ARM7TDMI::determineThumbInstruction(uint16_t instruction)
{
    uint32_t index = (instruction >> 6) & 0x3FF;

    return thumbTable[index];
}

void ARM7TDMI::flushPipeline()
{
    if (!registers->GetProgramStatusRegister().GetThumbState())
    {
        ExecutingInstruction = 0;
        DecodingInstruction = 0;
        FetchedInstruction = 0;

        ExecutingInstruction = Read32();
        DecodingInstruction = Read32();

        isFlushed = true;
    }
    else
    {
        ThumbExecutingInstruction = 0;
        ThumbDecodingInstruction = 0;

        ThumbExecutingInstruction = Read16();
        ThumbDecodingInstruction = Read16();

        isFlushed = true;
    }
}

bool ARM7TDMI::InterruptWaiting()
{
    uint16_t ie = memoryBus->read16Raw(0x04000200);
    uint16_t irqFlags = memoryBus->read16Raw(0x04000202);

    return (ie & irqFlags) != 0;
}

bool ARM7TDMI::InterruptPending()
{
    uint8_t ime = memoryBus->read8Raw(0x04000208);

    if (!(ime & 0x1))
        return false;
    if (registers->GetProgramStatusRegister().GetIRQDisable())
        return false;

    return InterruptWaiting();
}

void ARM7TDMI::EnterException(CPUMode mode, uint32_t vectorAddress, uint32_t returnAddress)
{
    uint32_t oldCpsr = registers->GetProgramStatusRegister().GetValue();

    registers->GetProgramStatusRegister().SetMode(mode);
    registers->GetSavedProgramStatusRegister().SetValue(oldCpsr);
    *registers->GetRegister(LINK_REGISTER) = returnAddress;

    registers->GetProgramStatusRegister().SetThumbState(false);
    registers->GetProgramStatusRegister().SetIRQDisable(true);

    *registers->GetRegister(PROGRAM_COUNTER) = vectorAddress;
    flushPipeline();
}

void ARM7TDMI::EnterInterrupt()
{
    uint32_t pc = *registers->GetRegister(PROGRAM_COUNTER);
    bool wasThumb = registers->GetProgramStatusRegister().GetThumbState();
    //LR needs nextInstr+4, arm's pipelined PC is 4 short of that, thumb's isn't
    uint32_t returnAddress = wasThumb ? pc : (pc - 4);
    EnterException(IRQ, 0x18, returnAddress);
}

void ARM7TDMI::HandleDataProcessingPCWrite(uint8_t destinationRegister, bool setConditionCodes)
{
    if (destinationRegister != PROGRAM_COUNTER)
        return;

    if (setConditionCodes)
        registers->GetProgramStatusRegister().SetValue(registers->GetSavedProgramStatusRegister().GetValue());

    flushPipeline();
}

bool ARM7TDMI::IsValueNegative(uint32_t Value)
{
    return (Value >> 31) & 1;
}

bool ARM7TDMI::IsValueZero(uint32_t Value)
{
    return Value == 0;
}

bool ARM7TDMI::IsCarryAddition(uint32_t Value1, uint32_t Value2)
{
    uint32_t result = Value1 + Value2;
    return result < Value1;
}

bool ARM7TDMI::IsCarrySubtraction(uint32_t Value1, uint32_t Value2)
{
    return (Value1 >= Value2);
}

bool ARM7TDMI::IsOverflowAddition(uint32_t Value1, uint32_t Value2)
{
    uint32_t result = Value1 + Value2;

    return ((Value1 ^ result) & (Value2 ^ result) & 0x80000000) != 0;
}

bool ARM7TDMI::IsOverflowSubtraction(uint32_t Value1, uint32_t Value2)
{
    uint32_t result = Value1 - Value2;
    return ((Value1 ^ Value2) & (Value1 ^ result) & 0x80000000) != 0;
}

uint32_t ARM7TDMI::ApplyShift(uint32_t value, uint8_t shiftType, uint8_t shiftAmount, bool& outCarry)
{
    outCarry = false;
    
    switch (shiftType) {
    case 0: // LSL
        if (shiftAmount == 0) {
            outCarry = false;
        } else if (shiftAmount < 32) {
            outCarry = (value >> (32 - shiftAmount)) & 1;
            value = value << shiftAmount;
        } else if (shiftAmount == 32) {
            outCarry = value & 1;
            value = 0;
        } else {
            outCarry = false;
            value = 0;
        }
        break;
    case 1: // LSR
        if (shiftAmount == 0) {
            outCarry = false;
        } else if (shiftAmount < 32) {
            outCarry = (value >> (shiftAmount - 1)) & 1;
            value = value >> shiftAmount;
        } else if (shiftAmount == 32) {
            outCarry = (value >> 31) & 1;
            value = 0;
        } else {
            outCarry = false;
            value = 0;
        }
        break;
    case 2: // ASR
        if (shiftAmount == 0) {
            outCarry = false;
        } else if (shiftAmount < 32) {
            outCarry = (value >> (shiftAmount - 1)) & 1;
            value = (int32_t)value >> shiftAmount;
        } else {
            outCarry = (value >> 31) & 1;
            value = (value & 0x80000000) ? 0xFFFFFFFF : 0;
        }
        break;
    case 3: // ROR
        if (shiftAmount == 0) {
            outCarry = false;
        } else {
            shiftAmount %= 32;
            outCarry = (value >> (shiftAmount - 1)) & 1;
            value = (value >> shiftAmount) | (value << (32 - shiftAmount));
        }
        break;
    }
    
    return value;
}

uint32_t ARM7TDMI::CalculateRotatedOperand(uint32_t instruction, bool& outCarry)
{
    uint32_t imm = instruction & 0xFF;
    uint8_t rotate = (instruction >> 8) & 0xF;

    uint32_t shiftAmount = rotate * 2;

    if (shiftAmount == 0)
    {
        outCarry = false;
        return imm;
    }

    outCarry = (imm >> (shiftAmount - 1)) & 1;
    return (imm >> shiftAmount) | (imm << (32 - shiftAmount));
}

void ARM7TDMI::armDataProcessing(uint32_t instruction)
{
    bool isImmediate = (instruction >> 25) & 0x1;
    bool setConditionCodes = (instruction >> 20) & 0x1;
    
    uint8_t opCode = (instruction >> 21) & 0xF;

    uint8_t Operand1Register = (instruction >> 16) & 0xF;
    uint8_t DestinationRegister = (instruction >> 12) & 0xF;

    uint32_t Operand2Val = instruction & 0x7FF;

    uint32_t Operand2 = Operand2Val;

    bool shiftCarry = registers->GetProgramStatusRegister().GetCarry();

    if (isImmediate)
    {
        Operand2 = CalculateRotatedOperand(instruction, shiftCarry);
    }
    else
    {
        uint8_t rm = instruction & 0xF;
        uint8_t shiftType = (instruction >> 5) & 3;
        bool shiftImmFlag = (instruction >> 4) & 1;

        uint32_t value = *registers->GetRegister(rm);

        uint8_t shiftAmount = 0;

        if (!shiftImmFlag)
        {
            shiftAmount = (instruction >> 7) & 0x1F;
        }
        else
        {
            uint8_t rs = (instruction >> 8) & 0xF;
            shiftAmount = *registers->GetRegister(rs) & 0xFF;
        }

        value = ApplyShift(value, shiftType, shiftAmount, shiftCarry);

        Operand2 = value;
    }

    uint32_t Operand1 = *registers->GetRegister(Operand1Register);

    switch (opCode)
    {
    case 0b0000:
        //AND
        {
            uint32_t result = Operand1 & Operand2;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b0001:
        //EOR
        {
            uint32_t result = Operand1 ^ Operand2;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b0010:
        //SUB
        {
            uint32_t result = Operand1 - Operand2;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(Operand1, Operand2));
                registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(Operand1, Operand2));
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b0011:
        //RSB (reverse subtract: Rd = Operand2 - Operand1)
        {
            uint32_t result = Operand2 - Operand1;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(Operand2, Operand1));
                registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(Operand2, Operand1));
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b0100:
        //ADD
        {
            uint32_t result = Operand1 + Operand2;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(IsCarryAddition(Operand1, Operand2));
                registers->GetProgramStatusRegister().SetOverflow(IsOverflowAddition(Operand1, Operand2));
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b0101:
        //ADC (Rd = Operand1 + Operand2 + Carry)
        {
            uint32_t carryIn = registers->GetProgramStatusRegister().GetCarry() ? 1 : 0;
            uint64_t wideResult = static_cast<uint64_t>(Operand1) + Operand2 + carryIn;
            uint32_t result = static_cast<uint32_t>(wideResult);
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(wideResult > 0xFFFFFFFFULL);
                registers->GetProgramStatusRegister().SetOverflow(((Operand1 ^ result) & (Operand2 ^ result) & 0x80000000) != 0);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b0110:
        //SBC (Rd = Operand1 - Operand2 - NOT(Carry), computed as Operand1 + ~Operand2 + Carry)
        {
            uint32_t carryIn = registers->GetProgramStatusRegister().GetCarry() ? 1 : 0;
            uint32_t invertedOperand2 = ~Operand2;
            uint64_t wideResult = static_cast<uint64_t>(Operand1) + invertedOperand2 + carryIn;
            uint32_t result = static_cast<uint32_t>(wideResult);
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(wideResult > 0xFFFFFFFFULL);
                registers->GetProgramStatusRegister().SetOverflow(((Operand1 ^ result) & (invertedOperand2 ^ result) & 0x80000000) != 0);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b0111:
        //RSC (Rd = Operand2 - Operand1 - NOT(Carry), computed as Operand2 + ~Operand1 + Carry)
        {
            uint32_t carryIn = registers->GetProgramStatusRegister().GetCarry() ? 1 : 0;
            uint32_t invertedOperand1 = ~Operand1;
            uint64_t wideResult = static_cast<uint64_t>(Operand2) + invertedOperand1 + carryIn;
            uint32_t result = static_cast<uint32_t>(wideResult);
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(wideResult > 0xFFFFFFFFULL);
                registers->GetProgramStatusRegister().SetOverflow(((Operand2 ^ result) & (invertedOperand1 ^ result) & 0x80000000) != 0);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b1000:
        //TST
        {
            uint32_t result = Operand1 & Operand2;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
            registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            return;
        }
    case 0b1001:
        //TEQ
        {
            uint32_t result = Operand1 ^ Operand2;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
            registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            return;
        }
    case 0b1010:
        //CMP
        {
            uint32_t result = Operand1 - Operand2;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
            registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(Operand1, Operand2));
            registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(Operand1, Operand2));
            return;
        }
    case 0b1011:
        //CMN
        {
            uint32_t result = Operand1 + Operand2;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
            registers->GetProgramStatusRegister().SetCarry(IsCarryAddition(Operand1, Operand2));
            registers->GetProgramStatusRegister().SetOverflow(IsOverflowAddition(Operand1, Operand2));
            return;
        }
    case 0b1100:
        //ORR
        {
            uint32_t result = Operand1 | Operand2;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b1101:
        //MOV
        {
            *registers->GetRegister(DestinationRegister) = Operand2;
            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(Operand2));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Operand2));
                registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b1110:
        {
            //BIC
            uint32_t result = Operand1 & ~Operand2;
            *registers->GetRegister(DestinationRegister) = result;
            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    case 0b1111:
        //MVN
        {
            uint32_t result = ~Operand2;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(shiftCarry);
            }
            HandleDataProcessingPCWrite(DestinationRegister, setConditionCodes);
            return;
        }
    }
}

void ARM7TDMI::armMultiply(uint32_t instruction)
{
    bool accumulate = (instruction >> 21) & 0x1;
    bool setConditionCodes = (instruction >> 20) & 0x1;

    uint8_t destinationRegister = (instruction >> 16) & 0xF;
    uint8_t accumulateRegister = (instruction >> 12) & 0xF;
    uint8_t operandRegisterS = (instruction >> 8) & 0xF;
    uint8_t operandRegisterM = instruction & 0xF;

    uint32_t result = *registers->GetRegister(operandRegisterM) * *registers->GetRegister(operandRegisterS);

    if (accumulate)
        result += *registers->GetRegister(accumulateRegister);

    *registers->GetRegister(destinationRegister) = result;

    if (setConditionCodes)
    {
        registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
        registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
        //carry unchanged, undefined after MUL/MLA
    }
}

void ARM7TDMI::armMultiplyLong(uint32_t instruction)
{
    bool signedOp = (instruction >> 22) & 0x1;
    bool accumulate = (instruction >> 21) & 0x1;
    bool setConditionCodes = (instruction >> 20) & 0x1;

    uint8_t highRegister = (instruction >> 16) & 0xF;
    uint8_t lowRegister = (instruction >> 12) & 0xF;
    uint8_t operandRegisterS = (instruction >> 8) & 0xF;
    uint8_t operandRegisterM = instruction & 0xF;

    uint32_t valueM = *registers->GetRegister(operandRegisterM);
    uint32_t valueS = *registers->GetRegister(operandRegisterS);

    uint64_t product;
    if (signedOp)
        product = static_cast<uint64_t>(
            static_cast<int64_t>(static_cast<int32_t>(valueM)) * static_cast<int64_t>(static_cast<int32_t>(valueS)));
    else
        product = static_cast<uint64_t>(valueM) * static_cast<uint64_t>(valueS);

    if (accumulate)
    {
        uint64_t existing = (static_cast<uint64_t>(*registers->GetRegister(highRegister)) << 32)
            | static_cast<uint64_t>(*registers->GetRegister(lowRegister));
        product += existing;
    }

    *registers->GetRegister(lowRegister) = static_cast<uint32_t>(product);
    *registers->GetRegister(highRegister) = static_cast<uint32_t>(product >> 32);

    if (setConditionCodes)
    {
        registers->GetProgramStatusRegister().SetZero(product == 0);
        registers->GetProgramStatusRegister().SetNegative((product >> 63) & 1);
        //carry and overflow unchanged, undefined after MULL/MLAL
    }
}

void ARM7TDMI::armSingleDataSwap(uint32_t instruction)
{
    bool isByte = (instruction >> 22) & 0x1;

    uint8_t BaseRegister = (instruction >> 16) & 0xF;
    uint8_t DestinationRegister = (instruction >> 12) & 0xF;
    uint8_t SourceRegister = instruction & 0xF;

    uint32_t address = *registers->GetRegister(BaseRegister);
    uint32_t sourceValue = *registers->GetRegister(SourceRegister);

    uint32_t loaded;
    if (isByte)
    {
        loaded = memoryBus->read8(address);
        memoryBus->write8(address, static_cast<uint8_t>(sourceValue));
    }
    else
    {
        loaded = LoadWord(address);
        StoreWord(address, sourceValue);
    }

    *registers->GetRegister(DestinationRegister) = loaded;
}

void ARM7TDMI::armBranchExchange(uint32_t instruction)
{
    uint8_t registerValue = instruction & 0xF;

    uint32_t address = *registers->GetRegister(registerValue);

    uint8_t newMode = address & 0x1;

    *registers->GetRegister(PROGRAM_COUNTER) = address & ~0x1;
    
    if (newMode == 0)
    {
        //ARM mode
        registers->GetProgramStatusRegister().SetThumbState(false);
    }
    else
    {
        //THUMB mode, scary!
        registers->GetProgramStatusRegister().SetThumbState(true);
    }

    flushPipeline();
}

void ARM7TDMI::armHalfwordDataTransfer(uint32_t instruction)
{
    bool preIndex = (instruction >> 24) & 0x1;
    bool upDown = (instruction >> 23) & 0x1;
    bool immediateOffset = (instruction >> 22) & 0x1;
    bool writeBack = (instruction >> 21) & 0x1;
    bool loadMemory = (instruction >> 20) & 0x1;

    uint8_t baseRegister = (instruction >> 16) & 0xF;
    uint8_t destinationRegister = (instruction >> 12) & 0xF;
    uint8_t sh = (instruction >> 5) & 0x3;

    uint32_t offset;
    if (immediateOffset)
    {
        offset = ((instruction >> 4) & 0xF0) | (instruction & 0xF);
    }
    else
    {
        uint8_t rm = instruction & 0xF;
        offset = *registers->GetRegister(rm);
    }

    if (!upDown) offset *= -1;

    uint32_t baseValue = *registers->GetRegister(baseRegister);
    uint32_t address = baseValue + (preIndex ? offset : 0);

    if (loadMemory)
    {
        uint32_t value;
        switch (sh)
        {
        case 0b01: //LDRH
            value = memoryBus->read16(address);
            break;
        case 0b10: //LDRSB
            value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(memoryBus->read8(address))));
            break;
        case 0b11: //LDRSH
            value = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(memoryBus->read16(address))));
            break;
        default:
            throw std::runtime_error("Invalid halfword data transfer SH field for load: " + std::to_string(sh));
        }
        *registers->GetRegister(destinationRegister) = value;
    }
    else
    {
        //STRH
        memoryBus->write16(address, static_cast<uint16_t>(*registers->GetRegister(destinationRegister)));
    }

    // Same post-index-always-writes-back rule as armSingleDataTransfer.
    if (!preIndex)
    {
        *registers->GetRegister(baseRegister) = baseValue + offset;
    }
    else if (writeBack)
    {
        *registers->GetRegister(baseRegister) = address;
    }
}

//LDR/STR
void ARM7TDMI::armSingleDataTransfer(uint32_t instruction)
{
    bool loadMemory = (instruction >> 20) & 0x1;
    bool writeBack = (instruction >> 21) & 0x1;
    bool byteWord = (instruction >> 22) & 0x1;
    bool upDown = (instruction >> 23) & 0x1;
    bool prePostIndex = (instruction >> 24) & 0x1;
    bool registerValue = (instruction >> 25) & 0x1;

    uint8_t baseRegister = (instruction >> 16) & 0xF;
    uint8_t destinationRegister = (instruction >> 12) & 0xF;

    uint32_t offsetOp = instruction & 0xFFF;

    //handles immediate
    uint32_t offset = offsetOp;

    bool shiftCarry = registers->GetProgramStatusRegister().GetCarry();
    if (registerValue)
    {
        uint8_t rm = instruction & 0xF;
        uint8_t shiftType = (instruction >> 5) & 3;
        uint8_t shiftAmount = (instruction >> 7) & 0x1F;

        uint32_t value = *registers->GetRegister(rm);

        offset = ApplyShift(value, shiftType, shiftAmount, shiftCarry);
    }

    if (!upDown) offset *= -1;

    uint32_t baseValue = *registers->GetRegister(baseRegister);

    //LDR
    uint32_t address = baseValue + (prePostIndex ? offset : 0);
    if (loadMemory)
    {
        if (!byteWord)
        {
            uint32_t word = LoadWord(address);

            *registers->GetRegister(destinationRegister) = word;
        }
        else
        {
            uint8_t byte = memoryBus->read8(address);

            *registers->GetRegister(destinationRegister) = (uint32_t)byte;
        }
        
        if (destinationRegister == PROGRAM_COUNTER)
        {
            *registers->GetRegister(PROGRAM_COUNTER) &= ~static_cast<uint32_t>(3);
            flushPipeline();
        }
    }
    //STR
    else
    {
        if (!byteWord)
        {
            StoreWord(address, *registers->GetRegister(destinationRegister));
        }
        else
        {
            memoryBus->write8(address, (uint8_t)*registers->GetRegister(destinationRegister));
        }
    }
    
    if (!prePostIndex)
    {
        *registers->GetRegister(baseRegister) = baseValue + offset;
    }
    else if (writeBack)
    {
        *registers->GetRegister(baseRegister) = address;
    }
}

void ARM7TDMI::armBlockDataTransfer(uint32_t instruction)
{
    bool PreIndex = (instruction >> 24) & 1;
    bool UpBit = (instruction >> 23) & 1;
    bool ForceUser = (instruction >> 22) & 1;
    bool WriteBack = (instruction >> 21) & 1;
    bool bIsLoad = (instruction >> 20) & 1;

    uint8_t Rn = (instruction >> 16) & 0xF;

    CPUMode ForcedMode = None;

    //check if PC is not transferred, but S bit is set (should force user mode registers)
    if (ForceUser && !(instruction & (1 << 15)))
    {
        ForcedMode = User;
    }

    //collect number of set bits for registers, used for decrement
    int registerCount = 0;
    for (int i = 0; i <= 15; i++)
        if (instruction & (1 << i)) registerCount++;

    uint32_t BaseAddress = *registers->GetRegister(Rn);
    uint32_t CurrentAddress = BaseAddress;

    //when decrementing, we start at the very bottom and work our way up. this avoids needing a seperate loop to loop backwards
    if (!UpBit)
    {
        CurrentAddress -= 4 * registerCount;
        //post decrement covers base minus count plus four up to base, one word above
        //where pre decrement sits, so shift the whole run up when PreIndex is clear
        if (!PreIndex)
            CurrentAddress += 4;
    }

    //loop through all possible registers at the start of the instruction, this is the register list
    for (uint8_t i = 0; i <= 15; i++)
    {
        //check if we should operate on this bit (register)
        if (instruction & (1 << i))
        {
            //if pre index, increment before loading/storing
            if (PreIndex && UpBit)
                CurrentAddress += 4;

            if (bIsLoad)
            {
                uint32_t value = memoryBus->read32(CurrentAddress);
                *registers->GetRegister(i, ForcedMode) = value;

                //if we're loading PC, mask to word alignment (LDM doesn't interwork on
                //ARMv4T - only BX/BLX switch state) and flush the pipeline
                if (i == 15)
                {
                    *registers->GetRegister(PROGRAM_COUNTER) &= ~static_cast<uint32_t>(3);
                    flushPipeline();
                }
            }
            else
            {
                //just write ts value nothing special
                uint32_t value = *registers->GetRegister(i, ForcedMode);
                memoryBus->write32(CurrentAddress, value);
            }

            //if post index, increment after loading/storing
            //when not up, we always increment here, and never elsewhere. this is because we already handled the decrement case
            if ((!PreIndex && UpBit) || !UpBit)
                CurrentAddress += 4;   
        }
    }

    //a load that names the base in its own register list keeps the loaded value, the
    //writeback is dropped rather than overwriting it
    const bool baseWasLoaded = bIsLoad && (instruction & (1 << Rn)) != 0;

    //if writeback bit is set, we need to set the final address to the register that was used for the initial address
    if (WriteBack && !baseWasLoaded)
    {
        if (UpBit)
            *registers->GetRegister(Rn) = CurrentAddress;
        else
            *registers->GetRegister(Rn) = BaseAddress - 4 * registerCount;
    }

    //if force user bit is set, and PC is set to be moved, we need to restore SPSR into CPSR
    if (ForceUser && instruction & (1 << 15))
    {
        uint32_t SPSR = registers->GetSavedProgramStatusRegister().GetValue();
        registers->GetProgramStatusRegister().SetValue(SPSR);
    }
}

void ARM7TDMI::armBranch(uint32_t instruction)
{
    bool withLink = (instruction >> 24) & 1;

    if (withLink)
    {
        //pc points 2 instructions ahead, next instruction is 1 instruction ahead!
        uint32_t nextInstruction = *registers->GetRegister(PROGRAM_COUNTER) - 4;

        *registers->GetRegister(LINK_REGISTER) = nextInstruction;
    }

    int32_t offset24 = static_cast<int32_t>(instruction & 0xFFFFFF);

    int32_t valueWithAppendedBits = offset24 << 2;

    int signBit = (offset24 >> 23) & 0x1;

    int32_t offset;
    if (signBit == 1)
    {
        offset = static_cast<int32_t>(valueWithAppendedBits | 0xFC000000);
    }
    else
    {
        offset = valueWithAppendedBits;
    }
    
    //set program counter to new offset
    *registers->GetRegister(PROGRAM_COUNTER) += offset;

    //flush the instruction pipeline, our position has changed!!
    flushPipeline();
}

void ARM7TDMI::armCoprocessorDataTransfer(uint32_t instruction)
{
    throw std::runtime_error("ARM instruction coprocessor data transfer (LDC/STC) not implemented.");
}

void ARM7TDMI::armCoprocessorDataOperation(uint32_t instruction)
{
    throw std::runtime_error("ARM instruction coprocessor data operation (CDP) not implemented.");
}

void ARM7TDMI::armCoprocessorRegisterTransfer(uint32_t instruction)
{
    throw std::runtime_error("ARM instruction coprocessor register transfer (MRC/MCR) not implemented.");
}

void ARM7TDMI::armSoftwareInterrupt(uint32_t instruction)
{
    uint32_t pc = *registers->GetRegister(PROGRAM_COUNTER);
    //arm's pipelined PC is nextInstr+4, SWI's return needs no further offset
    uint32_t returnAddress = pc - 4;
    EnterException(Supervisor, 0x08, returnAddress);
}

void ARM7TDMI::armUndefined(uint32_t instruction)
{
    throw std::runtime_error("Undefined ARM instruction: 0x" + 
        [instruction]{ 
            char buf[9]; 
            snprintf(buf, sizeof(buf), "%08X", instruction); 
            return std::string(buf); 
        }());
}

void ARM7TDMI::armPSRTransfer(uint32_t instruction)
{
    //bit 22 picks SPSR over CPSR, bit 21 picks MSR over MRS
    bool IsSavedRegister = (instruction >> 22) & 0x1;
    bool IsMsr = (instruction >> 21) & 0x1;

    if (!IsMsr)
    {
        //MRS - read CPSR/SPSR into a register
        uint32_t Value = IsSavedRegister ? registers->GetSavedProgramStatusRegister().GetValue()
            : registers->GetProgramStatusRegister().GetValue();

        uint8_t registerValue = (instruction >> 12) & 0xF;
        *registers->GetRegister(registerValue) = Value;
        return;
    }

    //MSR
    uint32_t Operand2;
    if ((instruction >> 25) & 0x1)
    {
        bool notNeeded = false;
        Operand2 = CalculateRotatedOperand(instruction, notNeeded);
    }
    else
    {
        //read the source in the current mode, before any mode change lands
        Operand2 = *registers->GetRegister(instruction & 0xF);
    }

    //one field mask bit per byte of the register, low to high: c x s f
    static constexpr uint32_t fieldBytes[4] = {0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000};
    uint8_t fieldMask = (instruction >> 16) & 0xF;

    uint32_t writeMask = 0;
    for (int i = 0; i < 4; i++)
    {
        if (fieldMask & (1 << i))
            writeMask |= fieldBytes[i];
    }

    if (IsSavedRegister)
    {
        ProgramStatusRegister spsr = registers->GetSavedProgramStatusRegister();
        spsr.SetValue((spsr.GetValue() & ~writeMask) | (Operand2 & writeMask));
        return;
    }

    if (registers->GetProgramStatusRegister().GetMode() == User)
        writeMask &= 0xFF000000;
    
    writeMask &= ~(1u << 5);

    ProgramStatusRegister cpsr = registers->GetProgramStatusRegister();
    cpsr.SetValue((cpsr.GetValue() & ~writeMask) | (Operand2 & writeMask));
}

void ARM7TDMI::thumbMoveShiftedRegister(uint16_t instruction)
{
    uint8_t OpCode = (instruction >> 11) & 0x3;
    uint8_t Offset = (instruction >> 6) & 0x1F;
    uint8_t SourceRegisterNum = (instruction >> 3) & 0x7;
    uint8_t DestinationRegisterNum = instruction & 0x7;

    if (OpCode > 2)
        throw std::runtime_error("Invalid Thumb Move Shifted Register opcode: " + std::to_string(OpCode));

    uint32_t* SourceRegister = registers->GetRegister(SourceRegisterNum);
    uint32_t* DestinationRegister = registers->GetRegister(DestinationRegisterNum);

    //0 means 32
    uint8_t shiftAmount = (Offset == 0 && OpCode != 0) ? 32 : Offset;

    bool carry = false;
    uint32_t value = ApplyShift(*SourceRegister, OpCode, shiftAmount, carry);
    *DestinationRegister = value;

    //always updates N Z C here, no S-bit like ARM shifts
    registers->GetProgramStatusRegister().SetZero(IsValueZero(value));
    registers->GetProgramStatusRegister().SetNegative(IsValueNegative(value));
    //LSL #0 does no shift, carry stays unchanged
    if (!(OpCode == 0 && Offset == 0))
        registers->GetProgramStatusRegister().SetCarry(carry);
}

void ARM7TDMI::thumbAddSubtract(uint16_t instruction)
{
    bool bIsImmediate = (instruction >> 10) & 0x1;
    bool bIsSubtract = (instruction >> 9) & 0x1;

    uint8_t Operand2 = (instruction >> 6) & 0x7;
    uint8_t sourceRegister = (instruction >> 3) & 0x7;
    uint8_t destinationRegister = instruction & 0x7;

    uint32_t Value = Operand2;

    if (!bIsImmediate)
    {
        Value = *registers->GetRegister(Operand2);
    }

    uint32_t sourceValue = *registers->GetRegister(sourceRegister);

    if (bIsSubtract)
    {
        //SUB
        uint32_t result = sourceValue - Value;
        *registers->GetRegister(destinationRegister) = result;
        registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
        registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
        registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(sourceValue, Value));
        registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(sourceValue, Value));
    }
    else
    {
        //ADD
        uint32_t result = sourceValue + Value;
        *registers->GetRegister(destinationRegister) = result;
        registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
        registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
        registers->GetProgramStatusRegister().SetCarry(IsCarryAddition(sourceValue, Value));
        registers->GetProgramStatusRegister().SetOverflow(IsOverflowAddition(sourceValue, Value));
    }
}

void ARM7TDMI::thumbMoveCompareAddSubtractImmediate(uint16_t instruction)
{
    uint8_t opCode = (instruction >> 11) & 0x3;
    uint8_t destRegister = (instruction >> 8) & 0x7;
    uint8_t offset = instruction & 0xFF;

    uint32_t registerValue = *registers->GetRegister(destRegister);

    switch (opCode)
    {
    case 0:
        {
            //MOV
            *registers->GetRegister(destRegister) = (uint32_t)offset;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(offset));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(offset));
            break;
        }
    case 1:
        {
            //CMP
            uint32_t result = registerValue - offset;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
            registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(registerValue, offset));
            registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(registerValue, offset));
            break;
        }
    case 2:
        {
            //ADD
            uint32_t result = registerValue + offset;
            *registers->GetRegister(destRegister) = result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
            registers->GetProgramStatusRegister().SetCarry(IsCarryAddition(registerValue, offset));
            registers->GetProgramStatusRegister().SetOverflow(IsOverflowAddition(registerValue, offset));
            break;
        }
    case 3:
        {
            //SUB
            uint32_t result = registerValue - offset;
            *registers->GetRegister(destRegister) = result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
            registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(registerValue, offset));
            registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(registerValue, offset));
            break;
        }
    }
}

void ARM7TDMI::thumbALUOperations(uint16_t instruction)
{
    //ALU opcode to execute
    uint8_t OpCode = (instruction >> 6) & 0xF;
    //Source register
    uint8_t Rs = (instruction >> 3) & 0x7;
    //Destination register
    uint8_t Rd = instruction & 0x7;

    uint32_t SourceValue = *registers->GetRegister(Rs);
    uint32_t DestinationValue = *registers->GetRegister(Rd);

    switch (OpCode)
    {
    case 0b0000:
        {
            //AND
            uint32_t Result = DestinationValue & SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b0001:
        {
            //EOR
            uint32_t Result = DestinationValue ^ SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b0010:
        {
            //LSL
            uint32_t Result = DestinationValue << SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b0011:
        {
            //LSR
            uint32_t Result = DestinationValue >> SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b0100:
        {
            //ASR
            uint32_t Result = (int32_t)DestinationValue >> SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b0101:
        {
            //ADC
            //ADC, carried out in 64 bits so a carry into the sum cannot be lost when
            //SourceValue is already 0xFFFFFFFF
            uint32_t carryIn = registers->GetProgramStatusRegister().GetCarry() ? 1 : 0;
            uint64_t wideResult = static_cast<uint64_t>(DestinationValue) + SourceValue + carryIn;
            uint32_t Result = static_cast<uint32_t>(wideResult);
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            registers->GetProgramStatusRegister().SetCarry(wideResult > 0xFFFFFFFFULL);
            registers->GetProgramStatusRegister().SetOverflow(((DestinationValue ^ Result) & (SourceValue ^ Result) & 0x80000000) != 0);
            break;
        }
    case 0b0110:
        {
            //SBC, Rd = Rd - Rs - NOT(Carry), computed as Rd + ~Rs + Carry like the ARM form
            uint32_t carryIn = registers->GetProgramStatusRegister().GetCarry() ? 1 : 0;
            uint32_t invertedSource = ~SourceValue;
            uint64_t wideResult = static_cast<uint64_t>(DestinationValue) + invertedSource + carryIn;
            uint32_t Result = static_cast<uint32_t>(wideResult);
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            registers->GetProgramStatusRegister().SetCarry(wideResult > 0xFFFFFFFFULL);
            registers->GetProgramStatusRegister().SetOverflow(((DestinationValue ^ Result) & (invertedSource ^ Result) & 0x80000000) != 0);
            break;
        }
    case 0b0111:
        {
            //ROR
            bool Carry = registers->GetProgramStatusRegister().GetCarry();
            uint8_t ShiftAmount = SourceValue & 0xFF;
            //shift type 3 is ROR, no need to make a new function!!
            uint32_t Result = ApplyShift(DestinationValue, 3, ShiftAmount, Carry);
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            registers->GetProgramStatusRegister().SetCarry(Carry);
            break;
        }
    case 0b1000:
        {
            //TST
            uint32_t Result = DestinationValue & SourceValue;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b1001:
        {
            //NEG
            uint32_t Result = 0 - SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b1010:
        {
            //CMP
            uint32_t Result = DestinationValue - SourceValue;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(DestinationValue, SourceValue));
            registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(DestinationValue, SourceValue));
            break;
        }
    case 0b1011:
        {
            uint32_t Result = DestinationValue + SourceValue;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            registers->GetProgramStatusRegister().SetCarry(IsCarryAddition(DestinationValue, SourceValue));
            registers->GetProgramStatusRegister().SetOverflow(IsOverflowAddition(DestinationValue, SourceValue));
            break; 
        }
    case 0b1100:
        {
            //ORR
            uint32_t Result = DestinationValue | SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b1101:
        {
            //MUL
            uint32_t Result = SourceValue * DestinationValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b1110:
        {
            //BIC
            uint32_t Result = DestinationValue & ~SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    case 0b1111:
        {
            //MVN
            uint32_t Result = ~SourceValue;
            *registers->GetRegister(Rd) = Result;
            registers->GetProgramStatusRegister().SetZero(IsValueZero(Result));
            registers->GetProgramStatusRegister().SetNegative(IsValueNegative(Result));
            break;
        }
    default:
        throw std::runtime_error("Thumb ALU operator out of bounds.");
    }
}

void ARM7TDMI::thumbHiRegisterOperations(uint16_t instruction)
{
    uint8_t OpCode = (instruction >> 8) & 0x3;
    bool H1 = (instruction >> 7) & 0x1;
    bool H2 = (instruction >> 6) & 0x1;

    uint8_t Rs = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;

    uint8_t RsMasked = Rs + (H2 ? 8 : 0);
    uint8_t RdMasked = Rd + (H1 ? 8 : 0);

    switch (OpCode)
    {
        case 0x00:
        {
                //ADD
                *registers->GetRegister(RdMasked) += *registers->GetRegister(RsMasked);
                if (RdMasked == PROGRAM_COUNTER)
                {
                    //PC writes here never change Thumb state, unlike BX
                    *registers->GetRegister(PROGRAM_COUNTER) &= ~0x1u;
                    flushPipeline();
                }
                break;
        }
        case 0x01:
        {
                //CMP
                uint32_t op1 = *registers->GetRegister(RdMasked);
                uint32_t op2 = *registers->GetRegister(RsMasked);
                uint32_t result = op1 - op2;
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(IsCarrySubtraction(op1, op2));
                registers->GetProgramStatusRegister().SetOverflow(IsOverflowSubtraction(op1, op2));
                break;
        }
        case 0x02:
        {
                //MOV
                *registers->GetRegister(RdMasked) = *registers->GetRegister(RsMasked);
                if (RdMasked == PROGRAM_COUNTER)
                {
                    *registers->GetRegister(PROGRAM_COUNTER) &= ~0x1u;
                    flushPipeline();
                }
                break;
        }
        case 0x03:
        {
                //BX
                uint32_t address = *registers->GetRegister(RsMasked);
                //first bit of address is thumb state. 1 == THUMB 0 == ARM
                registers->GetProgramStatusRegister().SetThumbState(address & 0x1);
                //set program counter address, aligned
                *registers->GetRegister(PROGRAM_COUNTER) = address & ~0x1;
                //flush pipeline to collect new instructions
                flushPipeline();
                break;
        }
        default:
        {
                throw std::runtime_error("thumb HI register operation opcode type is undefined.");
        }
    }
}

void ARM7TDMI::thumbPCRelativeLoad(uint16_t instruction)
{
    uint8_t destinationRegister = (instruction >> 8) & 0x7;
    uint8_t offsetOperand = instruction & 0xFF;

    uint32_t offset = offsetOperand * 4;

    //read pc aligned to word
    uint32_t PCOffset = *registers->GetRegister(PROGRAM_COUNTER) & ~3;

    uint32_t Address = PCOffset + offset;

    *registers->GetRegister(destinationRegister) = memoryBus->read32(Address);
}

void ARM7TDMI::thumbLoadStoreRegisterOffset(uint16_t instruction)
{
    bool bIsLoad = (instruction >> 11) & 0x1;
    bool bIsByte = (instruction >> 10) & 0x1;

    uint8_t offsetRegister = (instruction >> 6) & 0x7;
    uint8_t baseRegister = (instruction >> 3) & 0x7;
    uint8_t destinationRegister = instruction & 0x7;

    uint32_t baseValue = *registers->GetRegister(baseRegister);
    uint32_t offsetValue = *registers->GetRegister(offsetRegister);

    uint32_t address = baseValue + offsetValue;

    if (bIsLoad)
    {
        if (bIsByte)
        {
            //LDRB
            uint8_t value = memoryBus->read8(address);
            *registers->GetRegister(destinationRegister) = value;
        }
        else
        {
            //LDR
            uint32_t value = memoryBus->read32(address);
            *registers->GetRegister(destinationRegister) = value;
        }
    }
    else
    {
        uint32_t value = *registers->GetRegister(destinationRegister);
        if (bIsByte)
        {
            //STRB
            memoryBus->write8(address, (uint8_t)value);
        }
        else
        {
            //STR
            memoryBus->write32(address, value);
        }
    }
}

void ARM7TDMI::thumbLoadStoreSignExtended(uint16_t instruction)
{
    //i dont know what this is
    bool HFlag = (instruction >> 11) & 0x1;
    bool bSignExtended = (instruction >> 10) & 0x1;
    uint8_t OffsetRegister = (instruction >> 6) & 0x7;
    uint8_t BaseRegister = (instruction >> 3) & 0x7;
    uint8_t DestinationRegister = instruction & 0x7;

    uint32_t Offset = *registers->GetRegister(OffsetRegister);
    uint32_t BaseAddress = *registers->GetRegister(BaseRegister);
    uint32_t Address = BaseAddress + Offset;

    if (bSignExtended)
    {
        if (HFlag)
        {
            //LDSH
            int16_t value = memoryBus->read16(Address);
            *registers->GetRegister(DestinationRegister) = (int32_t)value;
        }
        else
        {
            //LDSB
            int8_t value = memoryBus->read8(Address);
            *registers->GetRegister(DestinationRegister) = (int32_t)value;
        }
    }
    else
    {
        if (HFlag)
        {
            //LDRH
            uint16_t value = memoryBus->read16(Address);
            *registers->GetRegister(DestinationRegister) = value;
        }
        else
        {
            //STRH
            uint32_t value = *registers->GetRegister(DestinationRegister);
            memoryBus->write16(Address, (uint16_t)value);
        }
    }
}

void ARM7TDMI::thumbLoadStoreImmediateOffset(uint16_t instruction)
{
    bool bIsByte = (instruction >> 12) & 0x1;
    bool bIsLoad = (instruction >> 11) & 0x1;
    uint8_t Offset5 = (instruction >> 6) & 0x1F;
    uint8_t Rb = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;

    //real offset is 7 bits if its a word transfer, stored as Offset >> 2, so to get the real offset back we do Offset << 2.
    uint32_t Offset = bIsByte ? Offset5 : Offset5 << 2;
    
    uint32_t BaseAddress = *registers->GetRegister(Rb);
    uint32_t Address = BaseAddress + Offset;

    if (bIsLoad)
    {
        if (bIsByte)
        {
            uint8_t value = memoryBus->read8(Address);
            *registers->GetRegister(Rd) = value;
        }
        else
        {
            uint32_t value = memoryBus->read32(Address);
            *registers->GetRegister(Rd) = value;
        }

        if (Rd == PROGRAM_COUNTER)
        {
            flushPipeline();
        }
    }
    else
    {
        if (bIsByte)
        {
            memoryBus->write8(Address, *registers->GetRegister(Rd));
        }
        else
        {
            memoryBus->write32(Address, *registers->GetRegister(Rd));
        }
    }
}

void ARM7TDMI::thumbLoadStoreHalfword(uint16_t instruction)
{
    bool bIsLoad = (instruction >> 11) & 0x1;
    uint8_t Offset5 = (instruction >> 6) & 0x1F;
    uint8_t Rb = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;

    //Offset5 is stored as Imm >> 1, to reduce bits by one
    uint8_t Imm = Offset5 << 1;

    uint32_t BaseAddress = *registers->GetRegister(Rb);
    uint32_t Address = BaseAddress + Imm;

    if (bIsLoad)
    {
        //LDRH
        //cast 16 bit read to uint32_t to unset top bits automatically
        uint32_t Value = (uint32_t)memoryBus->read16(Address);
        *registers->GetRegister(Rd) = Value;
    }
    else
    {
        //STRH
        //since we're storing a halfword, mask out the top bits
        uint16_t ValueToStore = *registers->GetRegister(Rd) & 0xFFFF;
        //write the 16 valid bits to the address
        memoryBus->write16(Address, ValueToStore);
    }
}

void ARM7TDMI::thumbSPRelativeLoadStore(uint16_t instruction)
{
    //if we're loading
    bool bIsLoad = (instruction >> 11) & 0x1;
    //destination register
    uint8_t Rd = (instruction >> 8) & 0x7;
    
    uint8_t Word8 = instruction & 0xFF;
    //Offset = Word8 << 2 (x 4), since Word8 is stored as Offset >> 2
    uint32_t Offset = Word8 * 4;

    uint32_t CurrentStackPointer = *registers->GetRegister(STACK_POINTER);
    uint32_t Address = CurrentStackPointer + Offset;

    if (bIsLoad)
    {
        *registers->GetRegister(Rd) = memoryBus->read32(Address);
    }
    else
    {
        memoryBus->write32(Address, *registers->GetRegister(Rd));
    }
}

void ARM7TDMI::thumbLoadAddress(uint16_t instruction)
{
    bool useStackPointer = (instruction >> 11) & 0x1;
    uint8_t destinationRegister = (instruction >> 8) & 0x7;
    uint8_t word8 = instruction & 0xFF;

    //Word8 is stored as Offset >> 2
    uint32_t offset = word8 * 4;

    uint32_t baseValue;
    if (useStackPointer)
    {
        baseValue = *registers->GetRegister(STACK_POINTER);
    }
    else
    {
        //read pc aligned to word, matching thumbPCRelativeLoad's convention
        baseValue = *registers->GetRegister(PROGRAM_COUNTER) & ~3;
    }

    *registers->GetRegister(destinationRegister) = baseValue + offset;
}

void ARM7TDMI::thumbAddOffsetToSP(uint16_t instruction)
{
    //if the offset is negative
    bool subtract = (instruction >> 7) & 0x1;
    uint8_t SWord7 = instruction & 0x7F;

    //extend 7 bit to 9 bit, shift left twice
    uint16_t SWord9 = SWord7 * 4;

    if (subtract)
    {
        *registers->GetRegister(STACK_POINTER) -= SWord9; 
    }
    else
    {
        *registers->GetRegister(STACK_POINTER) += SWord9;
    }
}

void ARM7TDMI::thumbPushPopRegisters(uint16_t instruction)
{
    //Store or Load
    bool bLoad = (instruction >> 11) & 0x1;
    //Store LR/Load PC
    bool R = (instruction >> 8) & 0x1;
    
    //list of registers to load/store starts at bit 0. 1 bit for each register from R0 to R7

    //POP
    if (bLoad)
    {
        //load from lowest bit first, R0, so loop forwards
        for (int i = 0; i <= 7; i++)
        {
            //check if bit i is set in instruction, would correspond to Ri in the register list.
            //if set, load/store this register.
            if (instruction & (1 << i))
            {
                //read register from stack pointer
                *registers->GetRegister(i) = memoryBus->read32(*registers->GetRegister(STACK_POINTER));
                //increment stack pointer to next value
                *registers->GetRegister(STACK_POINTER) += 4;
            }
        }

        if (R)
        {
            //#mask
            uint32_t returnAddress = memoryBus->read32(*registers->GetRegister(STACK_POINTER));
            *registers->GetRegister(PROGRAM_COUNTER) = returnAddress & ~1u;
            *registers->GetRegister(STACK_POINTER) += 4;
            flushPipeline();
        }
    }
    //PUSH
    else
    {
        if (R)
        {
            *registers->GetRegister(STACK_POINTER) -= 4;
            memoryBus->write32(*registers->GetRegister(STACK_POINTER), *registers->GetRegister(LINK_REGISTER));
        }

        //store from highest bit first, R7, so loop backward
        for (int i = 7; i >= 0; i--)
        {
            //same as POP/Store
            if (instruction & (1 << i))
            {
                //decrement stack pointer first.
                *registers->GetRegister(STACK_POINTER) -= 4;
                memoryBus->write32(*registers->GetRegister(STACK_POINTER), *registers->GetRegister(i));
            }
        }
    }
}

void ARM7TDMI::thumbMultipleLoadStore(uint16_t instruction)
{
    //Store or Load
    bool bIsLoad = (instruction >> 11) & 0x1;
    //base register
    uint8_t Rb = (instruction >> 8) & 0x7;
    //list of registers to load/store starts at bit 0. 1 bit for each register from R0 to R7
    uint8_t RegisterList = instruction & 0xFF;

    uint32_t Address = *registers->GetRegister(Rb);

    if (bIsLoad)
    {
        //LDMIA
        for (int i = 0; i <= 7; i++)
        {
            if (RegisterList & (1 << i))
            {
                *registers->GetRegister(i) = memoryBus->read32(Address);
                Address += 4;
            }
        }
    }
    else
    {
        //STMIA
        for (int i = 0; i <= 7; i++)
        {
            if (RegisterList & (1 << i))
            {
                memoryBus->write32(Address, *registers->GetRegister(i));
                Address += 4;
            }
        }
    }

    //write back addr
    if (!bIsLoad || !(RegisterList & (1 << Rb)))
    {
        *registers->GetRegister(Rb) = Address;
    }
}

void ARM7TDMI::thumbConditionalBranch(uint16_t instruction)
{
    ConditionCode condition = static_cast<ConditionCode>(instruction >> 8 & 0xF);
    if (checkCondition(condition))
    {
        int8_t offset = static_cast<int8_t>(instruction & 0xFF);
        
        uint32_t* programCounter = registers->GetRegister(PROGRAM_COUNTER);
        *programCounter = *programCounter + offset * 2;
        flushPipeline();
    }
}

void ARM7TDMI::thumbSoftwareInterrupt(uint16_t instruction)
{
    uint32_t pc = *registers->GetRegister(PROGRAM_COUNTER);
    //thumb's pipelined PC is nextInstr+2, SWI's return needs no further offset
    uint32_t returnAddress = pc - 2;
    EnterException(Supervisor, 0x08, returnAddress);
}

void ARM7TDMI::thumbUnconditionalBranch(uint16_t instruction)
{
    uint16_t Offset11 = instruction & 0x7FF;
    
    int32_t SignedOffset11 = (Offset11 & 0x400)
        ? static_cast<int32_t>(Offset11 | 0xFFFFF800)
        : static_cast<int32_t>(Offset11);
    int32_t Offset = SignedOffset11 << 1;

    *registers->GetRegister(PROGRAM_COUNTER) += Offset;
    flushPipeline();
}

void ARM7TDMI::thumbLongBranchWithLink(uint16_t instruction)
{
    bool isLow = (instruction >> 11) & 0x1;
    uint32_t offset = instruction & 0x7FF;

    //first instruction in 2 instruction set, set up the link register with the lower half of the address
    if (!isLow)
    {
        //check bit 10
        bool shouldShift = offset & 0x400;
        //fill top bits if we should shift, to sign extend
        int32_t signedOffset = shouldShift ? offset | 0xFFFFF800 : offset;
        //shift left 12 times
        signedOffset = signedOffset << 12;
        *registers->GetRegister(LINK_REGISTER) = *registers->GetRegister(PROGRAM_COUNTER) + signedOffset;
    }
    else
    {
        //get next instruction for the final link register value
        uint32_t nextInstruction = *registers->GetRegister(PROGRAM_COUNTER) - 2;
        
        uint32_t currentLinkRegisterValue = *registers->GetRegister(LINK_REGISTER);
        //LR = LR + (offset << 1), shift left once, align
        uint32_t newLinkRegisterValue = (currentLinkRegisterValue + (offset << 1)) & ~1;
        //set PC to new LR value
        *registers->GetRegister(PROGRAM_COUNTER) = newLinkRegisterValue;
        //set LR to final value, next instruction
        *registers->GetRegister(LINK_REGISTER) = nextInstruction | 1; // force thumb mode on address (bit 0 = 1)
        flushPipeline();
    }
}

void ARM7TDMI::thumbUndefined(uint16_t instruction)
{
    throw std::runtime_error("Undefined Thumb instruction: 0x" +
        [instruction]{
            char buf[5];
            snprintf(buf, sizeof(buf), "%04X", instruction);
            return std::string(buf);
        }());
}