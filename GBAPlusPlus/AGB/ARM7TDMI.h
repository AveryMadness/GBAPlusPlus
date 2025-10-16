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
    void runCpuStep();

private:

    //helper read and write functions
    uint32_t Read32();
    
    //typedef for calling instruction function
    typedef void (ARM7TDMI::*Instruction)(uint32_t);

    std::array<Instruction, 4096> armTable;

    void buildArmTable();

    bool checkCondition(ConditionCode condition);

    Instruction determineArmInstruction(uint32_t instruction);
    
    MemoryBus* memoryBus;
    ARMRegisters* registers;

    uint32_t FetchedInstruction = 0;
    uint32_t DecodingInstruction = 0;
    uint32_t ExecutingInstruction = 0;

    bool isFlushed = false;

    void flushPipeline();

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
};
