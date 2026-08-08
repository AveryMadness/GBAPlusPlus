#include "Disassembler.h"
#include <cstdio>

namespace Disassembler
{
namespace
{
    const char* ConditionSuffixes[16] = {
        "EQ","NE","CS","CC","MI","PL","VS","VC","HI","LS","GE","LT","GT","LE","",""
    };

    std::string Hex(uint32_t value)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%X", value);
        return std::string(buf);
    }

    std::string RegName(uint8_t r)
    {
        switch (r) {
        case 13: return "SP";
        case 14: return "LR";
        case 15: return "PC";
        default: return "R" + std::to_string(r);
        }
    }

    std::string ConditionOf(uint8_t condBits)
    {
        return condBits <= 13 ? std::string(ConditionSuffixes[condBits]) : std::string();
    }

    uint32_t RotatedImmediate(uint32_t instruction)
    {
        uint8_t imm = instruction & 0xFF;
        uint8_t rotate = (instruction >> 8) & 0xF;
        uint32_t shiftAmount = rotate * 2;
        return shiftAmount == 0 ? imm : ((imm >> shiftAmount) | (imm << (32 - shiftAmount)));
    }

    std::string DisasmGeneric(const char* name, uint32_t instruction)
    {
        return std::string(name) + " (raw " + Hex(instruction) + ")";
    }

    // ── ARM ──────────────────────────────────────────────────────────────────

    std::string DisasmDataProcessing(uint32_t instr, const std::string& cond)
    {
        bool isImmediate = (instr >> 25) & 0x1;
        bool setConditionCodes = (instr >> 20) & 0x1;
        uint8_t opCode = (instr >> 21) & 0xF;
        uint8_t Rn = (instr >> 16) & 0xF;
        uint8_t Rd = (instr >> 12) & 0xF;

        static const char* mnemonics[16] = {
            "AND","EOR","SUB","RSB","ADD","ADC","SBC","RSC",
            "TST","TEQ","CMP","CMN","ORR","MOV","BIC","MVN"
        };

        std::string operand2Text;
        if (isImmediate)
        {
            operand2Text = "#" + Hex(RotatedImmediate(instr));
        }
        else
        {
            uint8_t rm = instr & 0xF;
            uint8_t shiftType = (instr >> 5) & 3;
            bool shiftByRegister = (instr >> 4) & 1;
            static const char* shiftNames[4] = { "LSL", "LSR", "ASR", "ROR" };

            if (!shiftByRegister)
            {
                uint8_t shiftAmount = (instr >> 7) & 0x1F;
                operand2Text = shiftAmount == 0
                    ? RegName(rm)
                    : RegName(rm) + ", " + shiftNames[shiftType] + " #" + std::to_string(shiftAmount);
            }
            else
            {
                uint8_t rs = (instr >> 8) & 0xF;
                operand2Text = RegName(rm) + ", " + shiftNames[shiftType] + " " + RegName(rs);
            }
        }

        bool isCompareOnly = opCode >= 8 && opCode <= 11;
        bool showS = setConditionCodes && !isCompareOnly;
        std::string mnemonic = std::string(mnemonics[opCode]) + cond + (showS ? "S" : "");

        if (isCompareOnly)
            return mnemonic + " " + RegName(Rn) + ", " + operand2Text;
        //MOV, MVN
        if (opCode == 13 || opCode == 15)
            return mnemonic + " " + RegName(Rd) + ", " + operand2Text;
        return mnemonic + " " + RegName(Rd) + ", " + RegName(Rn) + ", " + operand2Text;
    }

    std::string DisasmMultiply(uint32_t instr, const std::string& cond)
    {
        bool accumulate = (instr >> 21) & 1;
        bool setConditionCodes = (instr >> 20) & 1;
        uint8_t Rd = (instr >> 16) & 0xF;
        uint8_t Rn = (instr >> 12) & 0xF;
        uint8_t Rs = (instr >> 8) & 0xF;
        uint8_t Rm = instr & 0xF;

        std::string mnemonic = std::string(accumulate ? "MLA" : "MUL") + cond + (setConditionCodes ? "S" : "");
        if (accumulate)
            return mnemonic + " " + RegName(Rd) + ", " + RegName(Rm) + ", " + RegName(Rs) + ", " + RegName(Rn);
        return mnemonic + " " + RegName(Rd) + ", " + RegName(Rm) + ", " + RegName(Rs);
    }

    std::string DisasmMultiplyLong(uint32_t instr, const std::string& cond)
    {
        bool signedOp = (instr >> 22) & 1;
        bool accumulate = (instr >> 21) & 1;
        bool setConditionCodes = (instr >> 20) & 1;
        uint8_t RdHi = (instr >> 16) & 0xF;
        uint8_t RdLo = (instr >> 12) & 0xF;
        uint8_t Rs = (instr >> 8) & 0xF;
        uint8_t Rm = instr & 0xF;

        std::string mnemonic = std::string(signedOp ? "S" : "U") + (accumulate ? "MLAL" : "MULL")
            + cond + (setConditionCodes ? "S" : "");
        return mnemonic + " " + RegName(RdLo) + ", " + RegName(RdHi) + ", " + RegName(Rm) + ", " + RegName(Rs);
    }

    std::string DisasmSingleDataSwap(uint32_t instr, const std::string& cond)
    {
        bool byteSwap = (instr >> 22) & 1;
        uint8_t Rn = (instr >> 16) & 0xF;
        uint8_t Rd = (instr >> 12) & 0xF;
        uint8_t Rm = instr & 0xF;

        return std::string("SWP") + cond + (byteSwap ? "B" : "") + " "
            + RegName(Rd) + ", " + RegName(Rm) + ", [" + RegName(Rn) + "]";
    }

    std::string DisasmBranchExchange(uint32_t instr, const std::string& cond)
    {
        uint8_t Rm = instr & 0xF;
        return "BX" + cond + " " + RegName(Rm);
    }

    std::string DisasmHalfwordDataTransfer(uint32_t instr, const std::string& cond)
    {
        bool pre = (instr >> 24) & 1;
        bool up = (instr >> 23) & 1;
        bool immediateOffset = (instr >> 22) & 1;
        bool writeBack = (instr >> 21) & 1;
        bool isLoad = (instr >> 20) & 1;
        uint8_t Rn = (instr >> 16) & 0xF;
        uint8_t Rd = (instr >> 12) & 0xF;
        uint8_t sh = (instr >> 5) & 0x3;

        std::string mnemonic;
        if (isLoad)
        {
            switch (sh) {
            case 1: mnemonic = "LDRH"; break;
            case 2: mnemonic = "LDRSB"; break;
            case 3: mnemonic = "LDRSH"; break;
            default: mnemonic = "LDR?"; break;
            }
        }
        else
        {
            mnemonic = (sh == 1) ? "STRH" : "STR?";
        }

        std::string offsetText;
        if (immediateOffset)
        {
            uint8_t offset = static_cast<uint8_t>((instr >> 4) & 0xF0) | (instr & 0xF);
            offsetText = std::string("#") + (up ? "" : "-") + Hex(offset);
        }
        else
        {
            uint8_t Rm = instr & 0xF;
            offsetText = std::string(up ? "" : "-") + RegName(Rm);
        }

        std::string addressText = pre
            ? ("[" + RegName(Rn) + ", " + offsetText + "]" + (writeBack ? "!" : ""))
            : ("[" + RegName(Rn) + "], " + offsetText);

        return mnemonic + cond + " " + RegName(Rd) + ", " + addressText;
    }

    std::string DisasmPSRTransfer(uint32_t instr, const std::string& cond)
    {
        uint8_t signature = (instr >> 16) & 0x3F;

        if (signature == 0b001111)
        {
            bool useSPSR = (instr >> 22) & 1;
            uint8_t Rd = (instr >> 12) & 0xF;
            return "MRS" + cond + " " + RegName(Rd) + ", " + (useSPSR ? "SPSR" : "CPSR");
        }

        bool isImmediate = (instr >> 25) & 1;
        bool useSPSR = (instr >> 22) & 1;

        std::string operandText = isImmediate
            ? ("#" + Hex(RotatedImmediate(instr)))
            : RegName(instr & 0xF);

        // Field mask (which of control/extension/status/flags get written) isn't
        // rendered precisely here - see the raw opcode on the trace line for that.
        return "MSR" + cond + " " + std::string(useSPSR ? "SPSR" : "CPSR") + "_fsxc, " + operandText;
    }

    std::string DisasmSingleDataTransfer(uint32_t instr, const std::string& cond)
    {
        bool loadMemory = (instr >> 20) & 1;
        bool writeBack = (instr >> 21) & 1;
        bool byteWord = (instr >> 22) & 1;
        bool up = (instr >> 23) & 1;
        bool pre = (instr >> 24) & 1;
        bool registerOffset = (instr >> 25) & 1;

        uint8_t Rn = (instr >> 16) & 0xF;
        uint8_t Rd = (instr >> 12) & 0xF;

        std::string offsetText;
        if (registerOffset)
        {
            uint8_t Rm = instr & 0xF;
            uint8_t shiftType = (instr >> 5) & 3;
            uint8_t shiftAmount = (instr >> 7) & 0x1F;
            static const char* shiftNames[4] = { "LSL", "LSR", "ASR", "ROR" };
            std::string shiftText = shiftAmount == 0
                ? ""
                : (", " + std::string(shiftNames[shiftType]) + " #" + std::to_string(shiftAmount));
            offsetText = std::string(up ? "" : "-") + RegName(Rm) + shiftText;
        }
        else
        {
            uint32_t offset = instr & 0xFFF;
            offsetText = std::string("#") + (up ? "" : "-") + Hex(offset);
        }

        std::string addressText = pre
            ? ("[" + RegName(Rn) + ", " + offsetText + "]" + (writeBack ? "!" : ""))
            : ("[" + RegName(Rn) + "], " + offsetText);

        std::string mnemonic = std::string(loadMemory ? "LDR" : "STR") + cond + (byteWord ? "B" : "");
        return mnemonic + " " + RegName(Rd) + ", " + addressText;
    }

    std::string DisasmBlockDataTransfer(uint32_t instr, const std::string& cond)
    {
        bool pre = (instr >> 24) & 1;
        bool up = (instr >> 23) & 1;
        bool forceUser = (instr >> 22) & 1;
        bool writeBack = (instr >> 21) & 1;
        bool isLoad = (instr >> 20) & 1;
        uint8_t Rn = (instr >> 16) & 0xF;

        const char* suffix = up ? (pre ? "IB" : "IA") : (pre ? "DB" : "DA");

        std::string regList = "{";
        bool first = true;
        for (int i = 0; i <= 15; i++)
        {
            if (instr & (1 << i))
            {
                if (!first) regList += ",";
                regList += RegName(static_cast<uint8_t>(i));
                first = false;
            }
        }
        regList += "}";

        std::string mnemonic = std::string(isLoad ? "LDM" : "STM") + cond + suffix;
        return mnemonic + " " + RegName(Rn) + (writeBack ? "!" : "") + ", " + regList + (forceUser ? "^" : "");
    }

    std::string DisasmBranch(uint32_t instr, uint32_t address, const std::string& cond)
    {
        bool withLink = (instr >> 24) & 1;
        int32_t offset24 = static_cast<int32_t>(instr & 0xFFFFFF);
        int32_t shifted = offset24 << 2;
        int signBit = (offset24 >> 23) & 1;
        int32_t offset = signBit ? static_cast<int32_t>(shifted | 0xFC000000) : shifted;

        // Matches the +8 pipeline-lookahead convention ARM7TDMI::armBranch relies on.
        uint32_t target = address + 8 + static_cast<uint32_t>(offset);

        return std::string(withLink ? "BL" : "B") + cond + " " + Hex(target);
    }

    std::string DisasmSWI(uint32_t instr, const std::string& cond)
    {
        uint32_t comment = instr & 0xFFFFFF;
        return "SWI" + cond + " #" + Hex(comment);
    }

    // ── Thumb ────────────────────────────────────────────────────────────────

    std::string DisasmMoveShiftedRegister(uint16_t instr)
    {
        uint8_t opCode = (instr >> 11) & 0x3;
        uint8_t offset = (instr >> 6) & 0x1F;
        uint8_t Rs = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;
        static const char* names[3] = { "LSL", "LSR", "ASR" };
        std::string mnemonic = opCode < 3 ? names[opCode] : "???";
        return mnemonic + " " + RegName(Rd) + ", " + RegName(Rs) + ", #" + std::to_string(offset);
    }

    std::string DisasmAddSubtract(uint16_t instr)
    {
        bool isImmediate = (instr >> 10) & 1;
        bool isSubtract = (instr >> 9) & 1;
        uint8_t operand2 = (instr >> 6) & 0x7;
        uint8_t Rs = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;

        std::string operandText = isImmediate ? ("#" + std::to_string(operand2)) : RegName(operand2);
        return std::string(isSubtract ? "SUB" : "ADD") + " " + RegName(Rd) + ", " + RegName(Rs) + ", " + operandText;
    }

    std::string DisasmMoveCompareAddSubtractImmediate(uint16_t instr)
    {
        uint8_t opCode = (instr >> 11) & 0x3;
        uint8_t Rd = (instr >> 8) & 0x7;
        uint8_t offset = instr & 0xFF;
        static const char* names[4] = { "MOV", "CMP", "ADD", "SUB" };
        return std::string(names[opCode]) + " " + RegName(Rd) + ", #" + Hex(offset);
    }

    std::string DisasmALUOperations(uint16_t instr)
    {
        uint8_t opCode = (instr >> 6) & 0xF;
        uint8_t Rs = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;
        static const char* names[16] = {
            "AND","EOR","LSL","LSR","ASR","ADC","SBC","ROR",
            "TST","NEG","CMP","CMN","ORR","MUL","BIC","MVN"
        };
        return std::string(names[opCode]) + " " + RegName(Rd) + ", " + RegName(Rs);
    }

    std::string DisasmHiRegisterOperations(uint16_t instr)
    {
        uint8_t opCode = (instr >> 8) & 0x3;
        bool H1 = (instr >> 7) & 1;
        bool H2 = (instr >> 6) & 1;
        uint8_t Rs = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;
        uint8_t RsFull = Rs + (H2 ? 8 : 0);
        uint8_t RdFull = Rd + (H1 ? 8 : 0);

        if (opCode == 3)
            return "BX " + RegName(RsFull);

        static const char* names[3] = { "ADD", "CMP", "MOV" };
        return std::string(names[opCode]) + " " + RegName(RdFull) + ", " + RegName(RsFull);
    }

    std::string DisasmPCRelativeLoad(uint16_t instr, uint32_t address)
    {
        uint8_t Rd = (instr >> 8) & 0x7;
        uint32_t offset = (instr & 0xFF) * 4;
        uint32_t target = ((address + 4) & ~3u) + offset;
        return "LDR " + RegName(Rd) + ", [PC, #" + Hex(offset) + "]  ; =" + Hex(target);
    }

    std::string DisasmLoadStoreRegisterOffset(uint16_t instr)
    {
        bool isLoad = (instr >> 11) & 1;
        bool isByte = (instr >> 10) & 1;
        uint8_t Ro = (instr >> 6) & 0x7;
        uint8_t Rb = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;
        std::string mnemonic = std::string(isLoad ? "LDR" : "STR") + (isByte ? "B" : "");
        return mnemonic + " " + RegName(Rd) + ", [" + RegName(Rb) + ", " + RegName(Ro) + "]";
    }

    std::string DisasmLoadStoreSignExtended(uint16_t instr)
    {
        bool H = (instr >> 11) & 1;
        bool S = (instr >> 10) & 1;
        uint8_t Ro = (instr >> 6) & 0x7;
        uint8_t Rb = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;

        const char* mnemonic = S ? (H ? "LDSH" : "LDSB") : (H ? "LDRH" : "STRH");
        return std::string(mnemonic) + " " + RegName(Rd) + ", [" + RegName(Rb) + ", " + RegName(Ro) + "]";
    }

    std::string DisasmLoadStoreImmediateOffset(uint16_t instr)
    {
        bool isByte = (instr >> 12) & 1;
        bool isLoad = (instr >> 11) & 1;
        uint8_t offset5 = (instr >> 6) & 0x1F;
        uint8_t Rb = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;
        uint32_t offset = isByte ? offset5 : (offset5 << 2);

        std::string mnemonic = std::string(isLoad ? "LDR" : "STR") + (isByte ? "B" : "");
        return mnemonic + " " + RegName(Rd) + ", [" + RegName(Rb) + ", #" + Hex(offset) + "]";
    }

    std::string DisasmLoadStoreHalfword(uint16_t instr)
    {
        bool isLoad = (instr >> 11) & 1;
        uint8_t offset5 = (instr >> 6) & 0x1F;
        uint8_t Rb = (instr >> 3) & 0x7;
        uint8_t Rd = instr & 0x7;
        uint32_t offset = static_cast<uint32_t>(offset5) << 1;

        return std::string(isLoad ? "LDRH" : "STRH") + " " + RegName(Rd) + ", [" + RegName(Rb) + ", #" + Hex(offset) + "]";
    }

    std::string DisasmSPRelativeLoadStore(uint16_t instr)
    {
        bool isLoad = (instr >> 11) & 1;
        uint8_t Rd = (instr >> 8) & 0x7;
        uint32_t offset = (instr & 0xFF) * 4;
        return std::string(isLoad ? "LDR" : "STR") + " " + RegName(Rd) + ", [SP, #" + Hex(offset) + "]";
    }

    std::string DisasmLoadAddress(uint16_t instr, uint32_t)
    {
        bool useSP = (instr >> 11) & 1;
        uint8_t Rd = (instr >> 8) & 0x7;
        uint32_t offset = (instr & 0xFF) * 4;
        return "ADD " + RegName(Rd) + ", " + (useSP ? "SP" : "PC") + ", #" + Hex(offset);
    }

    std::string DisasmAddOffsetToSP(uint16_t instr)
    {
        bool subtract = (instr >> 7) & 1;
        uint32_t offset = (instr & 0x7F) * 4;
        return std::string("ADD SP, #") + (subtract ? "-" : "") + Hex(offset);
    }

    std::string DisasmPushPopRegisters(uint16_t instr)
    {
        bool isLoad = (instr >> 11) & 1;
        bool extra = (instr >> 8) & 1;

        std::string regList = "{";
        bool first = true;
        for (int i = 0; i <= 7; i++)
        {
            if (instr & (1 << i))
            {
                if (!first) regList += ",";
                regList += RegName(static_cast<uint8_t>(i));
                first = false;
            }
        }
        if (extra)
        {
            if (!first) regList += ",";
            regList += isLoad ? "PC" : "LR";
        }
        regList += "}";

        return std::string(isLoad ? "POP " : "PUSH ") + regList;
    }

    std::string DisasmMultipleLoadStore(uint16_t instr)
    {
        bool isLoad = (instr >> 11) & 1;
        uint8_t Rb = (instr >> 8) & 0x7;

        std::string regList = "{";
        bool first = true;
        for (int i = 0; i <= 7; i++)
        {
            if (instr & (1 << i))
            {
                if (!first) regList += ",";
                regList += RegName(static_cast<uint8_t>(i));
                first = false;
            }
        }
        regList += "}";

        return std::string(isLoad ? "LDMIA " : "STMIA ") + RegName(Rb) + "!, " + regList;
    }

    std::string DisasmConditionalBranch(uint16_t instr, uint32_t address)
    {
        uint8_t condBits = (instr >> 8) & 0xF;
        int8_t offset = static_cast<int8_t>(instr & 0xFF);
        uint32_t target = address + 4 + static_cast<uint32_t>(static_cast<int32_t>(offset) * 2);

        return "B" + ConditionOf(condBits) + " " + Hex(target);
    }

    std::string DisasmSoftwareInterrupt(uint16_t instr)
    {
        uint8_t value = instr & 0xFF;
        return "SWI #" + Hex(value);
    }

    std::string DisasmUnconditionalBranch(uint16_t instr, uint32_t address)
    {
        uint16_t offset11 = instr & 0x7FF;
        int32_t signedOffset11 = (offset11 & 0x400)
            ? static_cast<int32_t>(offset11 | 0xFFFFF800)
            : static_cast<int32_t>(offset11);
        uint32_t target = address + 4 + static_cast<uint32_t>(signedOffset11 << 1);
        return "B " + Hex(target);
    }

    std::string DisasmLongBranchWithLink(uint16_t instr, uint32_t)
    {
        bool isLow = (instr >> 11) & 1;
        uint32_t offset = instr & 0x7FF;

        if (!isLow)
        {
            bool shouldSignExtend = offset & 0x400;
            int32_t signedOffset = shouldSignExtend
                ? static_cast<int32_t>(offset | 0xFFFFF800)
                : static_cast<int32_t>(offset);
            signedOffset <<= 12;
            return "BL.hi #" + Hex(static_cast<uint32_t>(signedOffset)) + "  ; LR = PC+off";
        }

        return "BL.lo #" + Hex(offset << 1) + "  ; PC = LR+off";
    }
}

std::string DisassembleARM(uint32_t instruction, uint32_t address)
{
    std::string cond = ConditionOf((instruction >> 28) & 0xF);

    uint32_t bits27_20 = (instruction >> 20) & 0xFF;
    uint32_t bits7_4 = (instruction >> 4) & 0xF;

    // Mirrors ARM7TDMI::buildArmTable's categorization exactly (same order, same bit
    // tests) so the disassembled mnemonic always agrees with whichever handler the
    // CPU will actually dispatch to.
    if ((bits27_20 & 0xC0) == 0x00)
    {
        if ((bits27_20 & 0xFC) == 0x00 && bits7_4 == 0x9)
            return DisasmMultiply(instruction, cond);
        if ((bits27_20 & 0xF8) == 0x08 && bits7_4 == 0x09)
            return DisasmMultiplyLong(instruction, cond);
        if ((bits27_20 & 0xFB) == 0x10 && bits7_4 == 0x9)
            return DisasmSingleDataSwap(instruction, cond);
        if (bits27_20 == 0x12 && bits7_4 == 0x01)
            return DisasmBranchExchange(instruction, cond);
        if ((bits7_4 & 0x9) == 0x9 && (bits27_20 & 0xE0) == 0x00)
            return DisasmHalfwordDataTransfer(instruction, cond);
        if ((bits27_20 & 0xFB) == 0x10 && bits7_4 == 0x0)
            return DisasmPSRTransfer(instruction, cond);
        if ((bits27_20 & 0xFB) == 0x12 && bits7_4 == 0x0)
            return DisasmPSRTransfer(instruction, cond);
        return DisasmDataProcessing(instruction, cond);
    }
    if ((bits27_20 & 0xC0) == 0x40)
        return DisasmSingleDataTransfer(instruction, cond);
    if ((bits27_20 & 0xE0) == 0x80)
        return DisasmBlockDataTransfer(instruction, cond);
    if ((bits27_20 & 0xE0) == 0xA0)
        return DisasmBranch(instruction, address, cond);
    if ((bits27_20 & 0xE0) == 0xC0)
        return DisasmGeneric("MCRR/MRRC", instruction);
    if ((bits27_20 & 0xF0) == 0xE0 && (bits7_4 & 0x1) == 0x0)
        return DisasmGeneric("CDP", instruction);
    if ((bits27_20 & 0xF0) == 0xE0 && (bits7_4 & 0x1) == 0x1)
        return DisasmGeneric("MRC/MCR", instruction);
    if ((bits27_20 & 0xF0) == 0xF0)
        return DisasmSWI(instruction, cond);

    return DisasmGeneric("UNDEFINED", instruction);
}

std::string DisassembleThumb(uint16_t instruction, uint32_t address)
{
    uint32_t bits15_13 = (instruction >> 13) & 0x7;
    uint32_t bits12_11 = (instruction >> 11) & 0x3;
    uint32_t bits10_8  = (instruction >> 8) & 0x7;
    uint32_t bits15_10 = (instruction >> 10) & 0x3F;

    // Mirrors ARM7TDMI::buildThumbTable's categorization exactly (same order, same
    // bit tests) for the same reason as the ARM side above.
    if (bits15_13 == 0b000 && bits12_11 != 0b11)
        return DisasmMoveShiftedRegister(instruction);
    if (bits15_13 == 0b000 && bits12_11 == 0b11)
        return DisasmAddSubtract(instruction);
    if (bits15_13 == 0b001)
        return DisasmMoveCompareAddSubtractImmediate(instruction);
    if (bits15_10 == 0b010000)
        return DisasmALUOperations(instruction);
    if (bits15_10 == 0b010001)
        return DisasmHiRegisterOperations(instruction);
    if (bits15_13 == 0b010 && bits12_11 == 0b01)
        return DisasmPCRelativeLoad(instruction, address);
    if (bits15_13 == 0b010 && bits12_11 == 0b10 && (bits10_8 & 0b010) == 0)
        return DisasmLoadStoreRegisterOffset(instruction);
    if (bits15_13 == 0b010 && bits12_11 == 0b10 && (bits10_8 & 0b010) != 0)
        return DisasmLoadStoreSignExtended(instruction);
    if (bits15_13 == 0b010 && bits12_11 == 0b11 && (bits10_8 & 0b010) == 0)
        return DisasmLoadStoreRegisterOffset(instruction);
    if (bits15_13 == 0b010 && bits12_11 == 0b11 && (bits10_8 & 0b010) != 0)
        return DisasmLoadStoreSignExtended(instruction);
    if (bits15_13 == 0b011)
        return DisasmLoadStoreImmediateOffset(instruction);
    if (bits15_13 == 0b100 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        return DisasmLoadStoreHalfword(instruction);
    if (bits15_13 == 0b100 && (bits12_11 == 0b10 || bits12_11 == 0b11))
        return DisasmSPRelativeLoadStore(instruction);
    if (bits15_13 == 0b101 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        return DisasmLoadAddress(instruction, address);
    if (bits15_13 == 0b101 && bits12_11 == 0b10 && bits10_8 == 0b000)
        return DisasmAddOffsetToSP(instruction);
    if (bits15_13 == 0b101 && (bits12_11 == 0b10 || bits12_11 == 0b11)
             && (bits10_8 == 0b100 || bits10_8 == 0b101))
        return DisasmPushPopRegisters(instruction);
    if (bits15_13 == 0b110 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        return DisasmMultipleLoadStore(instruction);
    if (bits15_13 == 0b110 && bits12_11 == 0b11 && bits10_8 == 0b111)
        return DisasmSoftwareInterrupt(instruction);
    if (bits15_13 == 0b110 && (bits12_11 == 0b10 || bits12_11 == 0b11))
        return DisasmConditionalBranch(instruction, address);
    if (bits15_13 == 0b111 && (bits12_11 == 0b00 || bits12_11 == 0b01))
        return DisasmUnconditionalBranch(instruction, address);
    if (bits15_13 == 0b111 && (bits12_11 == 0b10 || bits12_11 == 0b11))
        return DisasmLongBranchWithLink(instruction, address);

    return "UNDEFINED";
}

const char* ModeToString(CPUMode mode)
{
    switch (mode) {
    case User:       return "USR";
    case FIQ:        return "FIQ";
    case IRQ:        return "IRQ";
    case Supervisor: return "SVC";
    case Abort:      return "ABT";
    case Undefined:  return "UND";
    case System:     return "SYS";
    default:         return "???";
    }
}

}
