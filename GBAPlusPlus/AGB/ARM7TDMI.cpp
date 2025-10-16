#include "ARM7TDMI.h"

#include <Windows.h>

ARM7TDMI::ARM7TDMI(MemoryBus* memoryBus, ARMRegisters* registers)
{
    this->memoryBus = memoryBus;
    this->registers = registers;

    buildArmTable();
}

void ARM7TDMI::InitializeCpuForExecution()
{
    flushPipeline();
}

void ARM7TDMI::executeARMInstruction(uint32_t instruction)
{
    Instruction function = determineArmInstruction(instruction);
    (this->*function)(instruction);
}

void ARM7TDMI::runCpuStep()
{
    //execute instruction ready to be executed
    executeARMInstruction(ExecutingInstruction);

    //dont move up instructions after flushing pipeline.
    if (isFlushed)
    {
        isFlushed = false;
    }
    else
    {
        //move up the decoding instruction
        ExecutingInstruction = DecodingInstruction;
        //move up the fetched instruction
        DecodingInstruction = FetchedInstruction;
        //fetch a new instruction
        FetchedInstruction = Read32();
    }
}

uint32_t ARM7TDMI::Read32()
{
    uint32_t* programCounter = registers->GetRegister(PROGRAM_COUNTER);
    uint32_t value =  memoryBus->read32(*programCounter);

    *registers->GetRegister(PROGRAM_COUNTER) += 4;

    return value;
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
        return !cpsr.GetCarry() && cpsr.GetZero();
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

ARM7TDMI::Instruction ARM7TDMI::determineArmInstruction(uint32_t instruction)
{
    uint32_t bits27_20 = (instruction >> 20) & 0xFF;
    uint32_t bits7_4 = (instruction >> 4) & 0xF;

    //instruction pattern, index in table for instruction function
    uint32_t pattern = bits27_20 << 4 | bits7_4;

    return armTable[pattern];
}

void ARM7TDMI::flushPipeline()
{
    if (!registers->GetProgramStatusRegister().GetThumbState())
    {
        ExecutingInstruction = 0;
        DecodingInstruction = 0;
        FetchedInstruction = 0;

        //fetch new instruction
        FetchedInstruction = Read32();

        //move fetched into decoding, fetch new instruction
        DecodingInstruction = FetchedInstruction;
        FetchedInstruction = Read32();

        //move decoding into executing, fetched into decoding, fetch new instruction
        ExecutingInstruction = DecodingInstruction;
        DecodingInstruction = FetchedInstruction;
        FetchedInstruction = Read32();

        isFlushed = true;
    }
    else
    {
        throw std::exception("thumb mode is not implemented");
    }
}

void ARM7TDMI::armDataProcessing(uint32_t instruction)
{
    
}

void ARM7TDMI::armMultiply(uint32_t instruction)
{
}

void ARM7TDMI::armMultiplyLong(uint32_t instruction)
{
}

void ARM7TDMI::armSingleDataSwap(uint32_t instruction)
{
}

void ARM7TDMI::armBranchExchange(uint32_t instruction)
{
}

void ARM7TDMI::armHalfwordDataTransfer(uint32_t instruction)
{
}

void ARM7TDMI::armSingleDataTransfer(uint32_t instruction)
{
}

void ARM7TDMI::armBlockDataTransfer(uint32_t instruction)
{
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
    *registers->GetRegister(PROGRAM_COUNTER) += offset - 4;

    //flush the instruction pipeline, our position has changed!!
    flushPipeline();
}

void ARM7TDMI::armCoprocessorDataTransfer(uint32_t instruction)
{
}

void ARM7TDMI::armCoprocessorDataOperation(uint32_t instruction)
{
}

void ARM7TDMI::armCoprocessorRegisterTransfer(uint32_t instruction)
{
}

void ARM7TDMI::armSoftwareInterrupt(uint32_t instruction)
{
}

void ARM7TDMI::armUndefined(uint32_t instruction)
{
}

void ARM7TDMI::armPSRTransfer(uint32_t instruction)
{
}
