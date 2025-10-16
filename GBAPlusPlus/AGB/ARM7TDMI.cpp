#include "ARM7TDMI.h"

#include <iostream>
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
    ConditionCode condition = static_cast<ConditionCode>((ExecutingInstruction >> 28) & 0xF);

    if (checkCondition(condition))
    {
        //execute instruction ready to be executed
        executeARMInstruction(ExecutingInstruction);
    }

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

bool ARM7TDMI::IsValueNegative(uint32_t Value)
{
    if (Value & 0x80000000) return true;
    return false;
}

bool ARM7TDMI::IsValueZero(uint32_t Value)
{
    return Value == 0;
}

bool ARM7TDMI::IsCarryAddition(uint32_t Value1, uint32_t Value2)
{
    uint32_t result = Value1 + Value2;
    return (result < Value1) || (result > Value2); 
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

bool ARM7TDMI::IsCarryFromShifter(uint32_t operand2, uint8_t shiftType, uint8_t shiftAmount)
{
    // If no shift occurred, carry flag is unchanged
    if (shiftAmount == 0 && shiftType != 3) {  // ROR with 0 is special
        return registers->GetProgramStatusRegister().GetCarry();
    }
    
    switch (shiftType) {
    case 0: // LSL
        if (shiftAmount == 0) return registers->GetProgramStatusRegister().GetCarry();
        return (operand2 >> (32 - shiftAmount)) & 1;
        
    case 1: // LSR
        if (shiftAmount == 0) return (operand2 >> 31) & 1;
        return (operand2 >> (shiftAmount - 1)) & 1;
        
    case 2: // ASR
        if (shiftAmount == 0) return (operand2 >> 31) & 1;
        return (operand2 >> (shiftAmount - 1)) & 1;
        
    case 3: // ROR
        if (shiftAmount == 0) {
            // ROR #0 is actually RRX (rotate right with extend)
            return (registers->GetProgramStatusRegister().GetCarry()) ? 1 : 0;
        }
        return (operand2 >> (shiftAmount - 1)) & 1;
    }
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
        uint8_t imm = instruction & 0xFF;
        uint8_t rotate = (instruction >> 8) & 0xF;

        uint32_t shiftAmount = rotate * 2;

        if (shiftAmount == 0)
        {
            shiftCarry = false;
        }
        else
        {
            shiftCarry = (imm >> (shiftAmount - 1)) & 1;
        }
        
        Operand2 = (imm >> shiftAmount) | (imm << (32 - shiftAmount));
    }
    else
    {
        uint8_t rm = instruction & 0xF;
        uint8_t shiftType = (instruction >> 5) & 3;
        bool shiftImmFlag = (instruction >> 4) & 1;

        uint32_t value = *registers->GetRegister(rm);

        uint8_t shiftAmount = 0;

        if (shiftImmFlag)
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
            return;
        }
    case 0b0001:
        //EOR
        {
            return;
        }
    case 0b0010:
        //SUB
        {
            return;
        }
    case 0b0011:
        //RSB
        {
            return;
        }
    case 0b0100:
        //ADD
        {
            return;
        }
    case 0b0101:
        //ADC
        {
            return;
        }
    case 0b0110:
        //SBC
        {
            return;
        }
    case 0b0111:
        //RSC
        {
            return;
        }
    case 0b1000:
        //TST
        {
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
            return;
        }
    case 0b1100:
        //ORR
        {
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
            return;
        }
    case 0b1110:
        //BIC
        {
            return;
        }
    case 0b1111:
        //MVN
        {
            return;
        }
    }
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

    bool shiftCarry;
    if (registerValue)
    {
        uint8_t rm = instruction & 0xF;
        uint8_t shiftType = (instruction >> 5) & 3;
        uint8_t shiftAmount = (instruction >> 7) & 0x1F;

        uint32_t value = *registers->GetRegister(rm);

        offset = ApplyShift(value, shiftType, shiftAmount, shiftCarry);
    }

    if (!upDown) offset *= -1;

    //LDR
    uint32_t address = *registers->GetRegister(baseRegister) + (prePostIndex ? offset : 0);
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

    if (writeBack)
    {
        *registers->GetRegister(baseRegister) = address;
    }
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
