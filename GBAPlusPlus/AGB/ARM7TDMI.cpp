#include "ARM7TDMI.h"

#include <iostream>
#include <Windows.h>

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
    if (!registers->GetProgramStatusRegister().GetThumbState())
    {
        //arm mode
        ConditionCode condition = static_cast<ConditionCode>((ExecutingInstruction >> 28) & 0xF);

        if (checkCondition(condition))
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
        uint32_t bits10_8 = (i >> 2) & 0x7;
        uint32_t bits7_6 = i & 0x3;

        if (bits15_13 == 0b000 && bits12_11 != 0b11)
        {
            thumbTable[i] = &ARM7TDMI::thumbMoveShiftedRegister;
        }

        else if (bits15_13 == 0b000 && bits12_11 == 0b11)
        {
            thumbTable[i] = &ARM7TDMI::thumbAddSubtract;
        }

        else if (bits15_13 == 0b001)
        {
            thumbTable[i] = &ARM7TDMI::thumbMoveCompareAddSubtractImmediate;
        }

        else if (bits15_13 == 0b010 && bits12_11 == 0b00 && bits10_8 == 0b000)
        {
            thumbTable[i] = &ARM7TDMI::thumbALUOperations;
        }

        else if (bits15_13 == 0b010 && bits12_11 == 0b00 && bits10_8 == 0b001)
        {
            thumbTable[i] = &ARM7TDMI::thumbHiRegisterOperations;
        }

        else if (bits15_13 == 0b010 && bits12_11 == 0b01)
        {
            thumbTable[i] = &ARM7TDMI::thumbPCRelativeLoad;
        }

        else if (bits15_13 == 0b010 && bits12_11 == 0b10)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreRegisterOffset;
        }
        else if (bits15_13 == 0b010 && bits12_11 == 0b11)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreSignExtended;
        }
        else if (bits15_13 == 0b011)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreImmediateOffset;
        }
        else if (bits15_13 == 0b100 && (i >> 9) == 0b1000)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadStoreHalfword;
        }
        else if (bits15_13 == 0b100 && (i >> 9) == 0b1001)
        {
            thumbTable[i] = &ARM7TDMI::thumbSPRelativeLoadStore;
        }
        else if (bits15_13 == 0b101 && (i >> 9) == 0b1010)
        {
            thumbTable[i] = &ARM7TDMI::thumbLoadAddress;
        }
        else if (bits15_13 == 0b101 && (i >> 6) == 0b101100)
        {
            thumbTable[i] = &ARM7TDMI::thumbAddOffsetToSP;
        }
        else if (bits15_13 == 0b101 && bits12_11 == 0b11 && bits10_8 == 0b010)
        {
            thumbTable[i] = &ARM7TDMI::thumbPushPopRegisters;
        }
        else if (bits15_13 == 0b110 && (i >> 9) == 0b1100)
        {
            thumbTable[i] = &ARM7TDMI::thumbMultipleLoadStore;
        }
        else if (bits15_13 == 0b110 && (i >> 9) == 0b1101 && bits10_8 != 0b111)
        {
            thumbTable[i] = &ARM7TDMI::thumbConditionalBranch;
        }
        else if (bits15_13 == 0b110 && (i >> 2) == 0b11011111)
        {
            thumbTable[i] = &ARM7TDMI::thumbSoftwareInterrupt;
        }
        else if (bits15_13 == 0b111 && (i >> 9) == 0b1110)
        {
            thumbTable[i] = &ARM7TDMI::thumbUnconditionalBranch;
        }
        else if (bits15_13 == 0b111 && (i >> 9) == 0b1111)
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

uint32_t ARM7TDMI::CalculateRotatedOperand(uint32_t instruction, bool& outCarry)
{
    uint8_t imm = instruction & 0xFF;
    uint8_t rotate = (instruction >> 8) & 0xF;

    uint32_t shiftAmount = rotate * 2;

    if (shiftAmount == 0)
    {
        outCarry = false;
    }
    else
    {
        outCarry = (imm >> (shiftAmount - 1)) & 1;
    }
        
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
            uint32_t result = Operand1 + Operand2;
            *registers->GetRegister(DestinationRegister) = result;

            if (setConditionCodes)
            {
                registers->GetProgramStatusRegister().SetZero(IsValueZero(result));
                registers->GetProgramStatusRegister().SetNegative(IsValueNegative(result));
                registers->GetProgramStatusRegister().SetCarry(IsCarryAddition(Operand1, Operand2));
                registers->GetProgramStatusRegister().SetOverflow(IsOverflowAddition(Operand1, Operand2));
            }
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
    *registers->GetRegister(PROGRAM_COUNTER) += offset;

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
    uint8_t signature = (instruction >> 16) & 0x3F;


    if (signature == 0b001111)
    {
        //MRS - read CPSR/SPSR into register
        bool IsSavedRegister = (instruction >> 22) & 0x1;
        uint32_t Value = IsSavedRegister ? registers->GetSavedProgramStatusRegister().GetValue()
            : registers->GetProgramStatusRegister().GetValue();

        uint8_t registerValue = (instruction >> 12) & 0xF;
        *registers->GetRegister(registerValue) = Value;
    }
    else
    {
        //MSR - read register into CPSR
        bool IsImmediate = (instruction >> 25) & 0x1;
        uint8_t fieldMask = (instruction >> 16) & 0xF;

        bool flagsOnly = (fieldMask == 0xF) || ((instruction >> 22) & 1);

        //only transfer flags
        uint32_t Operand2 = 0;
        if (flagsOnly)
        {
            if (IsImmediate)
            {
                bool notNeeded = false;
                Operand2 = CalculateRotatedOperand(instruction, notNeeded);
            }
            else
            {
                //is register
                uint8_t registerValue = instruction & 0xF;
                Operand2 = *registers->GetRegister(registerValue);
            }

            registers->GetProgramStatusRegister().SetFlags(Operand2);
        }
        else
        {
            //is always register, cant be immediate
            uint8_t registerValue = instruction & 0xF;
            Operand2 = *registers->GetRegister(registerValue);

            if (fieldMask & 0x1) registers->GetProgramStatusRegister().SetControl(Operand2);
            if (fieldMask & 0x8) registers->GetProgramStatusRegister().SetFlags(Operand2);
        }
    }
}

void ARM7TDMI::thumbMoveShiftedRegister(uint16_t instruction)
{
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

    uint32_t registerValue = (uint8_t)*registers->GetRegister(destRegister);

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
}

void ARM7TDMI::thumbHiRegisterOperations(uint16_t instruction)
{
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
}

void ARM7TDMI::thumbLoadStoreImmediateOffset(uint16_t instruction)
{
}

void ARM7TDMI::thumbLoadStoreHalfword(uint16_t instruction)
{
}

void ARM7TDMI::thumbSPRelativeLoadStore(uint16_t instruction)
{
}

void ARM7TDMI::thumbLoadAddress(uint16_t instruction)
{
}

void ARM7TDMI::thumbAddOffsetToSP(uint16_t instruction)
{
}

void ARM7TDMI::thumbPushPopRegisters(uint16_t instruction)
{
}

void ARM7TDMI::thumbMultipleLoadStore(uint16_t instruction)
{
}

void ARM7TDMI::thumbConditionalBranch(uint16_t instruction)
{
}

void ARM7TDMI::thumbSoftwareInterrupt(uint16_t instruction)
{
}

void ARM7TDMI::thumbUnconditionalBranch(uint16_t instruction)
{
}

void ARM7TDMI::thumbLongBranchWithLink(uint16_t instruction)
{
}

void ARM7TDMI::thumbUndefined(uint16_t instruction)
{
}
