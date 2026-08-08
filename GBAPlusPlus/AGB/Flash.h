#pragma once
#include <array>
#include <cstdint>

class Flash
{
public:
    Flash();

    void Reset();

    uint8_t Read(uint32_t address) const;
    void Write(uint32_t address, uint8_t value);

    const uint8_t* Data() const;
    uint8_t* Data();
    size_t Size() const;

    bool ConsumeDirty();

    //Sanyo LE26FV10N1TS
    //not really sure if this ever changes...
    static constexpr uint8_t MANUFACTURER_ID = 0x62;
    static constexpr uint8_t DEVICE_ID = 0x13;

private:
    static constexpr size_t BANK_SIZE = 64 * 1024;
    static constexpr size_t SECTOR_SIZE = 0x1000;

    std::array<uint8_t, 2 * BANK_SIZE> data;

    uint8_t commandPhase = 0;
    bool idMode = false;
    bool erasePrepared = false;
    bool writePending = false;
    bool bankSwitchPending = false;
    uint8_t bank = 0;

    bool dirty = false;

    void HandleCommand(uint32_t offset, uint8_t value);
};
