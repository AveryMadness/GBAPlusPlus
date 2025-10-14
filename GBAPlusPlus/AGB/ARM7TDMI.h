#pragma once
#include <cstdint>

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
    ARM7TDMI(MemoryBus* memoryBus);
    MemoryBus* memoryBus;
};
