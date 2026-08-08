#pragma once
#include <cstdint>
#include <string>
#include "ARMRegisters.h"

//disassembler written by claude.... SORRY!!!! :(

// Pure, stateless decode of raw opcodes into assembly-like text for trace logging.
// Deliberately mirrors ARM7TDMI::buildArmTable/buildThumbTable's exact bit tests so a
// disassembled line always agrees with whichever handler the CPU will actually dispatch to,
// even where that handler is a stub or diverges from the "textbook" ARM encoding tables.
namespace Disassembler
{
    std::string DisassembleARM(uint32_t instruction, uint32_t address);
    std::string DisassembleThumb(uint16_t instruction, uint32_t address);
    const char* ModeToString(CPUMode mode);
}
