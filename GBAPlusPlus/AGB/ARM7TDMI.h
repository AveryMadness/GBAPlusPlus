#pragma once
#include <cstdint>

#include "ARMRegisters.h"
#include "MemoryBus.h"

enum ConditionCode : uint8_t
{
    Equal = 0b0000,
    NotEqual = 0b0001,
    CarrySet = 0b0010,
    CarryClear = 0b0011,
    Minus = 0b0100,
    Plus = 0b0101,
    Overflow = 0b0110,
    NoOverflow = 0b0111,
    UnsignedHigher = 0b1000,
    UnsignedLowerOrSame = 0b1001,
    SignedGreaterOrSame = 0b1010,
    SignedLessThan = 0b1011,
    SignedGreaterThan = 0b1100,
    SignedLessThanOrEqual = 0b1101,
    Always = 0b1110,
    Never = 0b1111
};

class ARM7TDMI
{
public:
    ARM7TDMI(MemoryBus* memoryBus, ARMRegisters* registers);
    void InitializeCpuForExecution();
    void executeARMInstruction(uint32_t instruction);
    void executeThumbInstruction(uint16_t instruction);
    void runCpuStep();

private:

    //helper read and write functions
    uint32_t Read32();
    uint16_t Read16();

    uint32_t LoadWord(uint32_t address);
    void StoreWord(uint32_t address, uint32_t value);
    
    //typedef for calling instruction function
    typedef void (ARM7TDMI::*ArmInstruction)(uint32_t);
    typedef void (ARM7TDMI::*ThumbInstruction)(uint16_t);
    
    std::array<ArmInstruction, 4096> armTable;
    std::array<ThumbInstruction, 1024> thumbTable;

    void buildArmTable();
    void buildThumbTable();

    bool checkCondition(ConditionCode condition);

    ArmInstruction determineArmInstruction(uint32_t instruction);
    ThumbInstruction determineThumbInstruction(uint16_t instruction);
    
    MemoryBus* memoryBus;
    ARMRegisters* registers;

    uint32_t FetchedInstruction = 0;
    uint32_t DecodingInstruction = 0;
    uint32_t ExecutingInstruction = 0;

    uint16_t ThumbDecodingInstruction = 0;
    uint16_t ThumbExecutingInstruction = 0;

    bool isFlushed = false;

    void flushPipeline();


    bool IsValueNegative(uint32_t Value);
    bool IsValueZero(uint32_t Value);

    bool IsCarryAddition(uint32_t Value1, uint32_t Value2);
    bool IsCarrySubtraction(uint32_t Value1, uint32_t Value2);
    bool IsCarryFromShifter(uint32_t operand2, uint8_t shiftType, uint8_t shiftAmount);

    bool IsOverflowAddition(uint32_t Value1, uint32_t Value2);
    bool IsOverflowSubtraction(uint32_t Value1, uint32_t Value2);

    uint32_t ApplyShift(uint32_t value, uint8_t shiftType, uint8_t shiftAmount, bool& outCarry);
    uint32_t CalculateRotatedOperand(uint32_t instruction, bool& outCarry);

    //arm instruction handlers
    void armDataProcessing(uint32_t instruction);
    void armMultiply(uint32_t instruction);
    void armMultiplyLong(uint32_t instruction);
    void armSingleDataSwap(uint32_t instruction);
    void armBranchExchange(uint32_t instruction);
    void armHalfwordDataTransfer(uint32_t instruction);
    void armSingleDataTransfer(uint32_t instruction);
    void armBlockDataTransfer(uint32_t instruction);
    void armBranch(uint32_t instruction);
    void armCoprocessorDataTransfer(uint32_t instruction);
    void armCoprocessorDataOperation(uint32_t instruction);
    void armCoprocessorRegisterTransfer(uint32_t instruction);
    void armSoftwareInterrupt(uint32_t instruction);
    void armUndefined(uint32_t instruction);
    void armPSRTransfer(uint32_t instruction);

    //thumb instruction handlers
    void thumbMoveShiftedRegister(uint16_t instruction);
    void thumbAddSubtract(uint16_t instruction);
    void thumbMoveCompareAddSubtractImmediate(uint16_t instruction);
    void thumbALUOperations(uint16_t instruction);
    void thumbHiRegisterOperations(uint16_t instruction);
    void thumbPCRelativeLoad(uint16_t instruction);
    void thumbLoadStoreRegisterOffset(uint16_t instruction);
    void thumbLoadStoreSignExtended(uint16_t instruction);
    void thumbLoadStoreImmediateOffset(uint16_t instruction);
    void thumbLoadStoreHalfword(uint16_t instruction);
    void thumbSPRelativeLoadStore(uint16_t instruction);
    void thumbLoadAddress(uint16_t instruction);
    void thumbAddOffsetToSP(uint16_t instruction);
    void thumbPushPopRegisters(uint16_t instruction);
    void thumbMultipleLoadStore(uint16_t instruction);
    void thumbConditionalBranch(uint16_t instruction);
    void thumbSoftwareInterrupt(uint16_t instruction);
    void thumbUnconditionalBranch(uint16_t instruction);
    void thumbLongBranchWithLink(uint16_t instruction);
    void thumbUndefined(uint16_t instruction);
};
