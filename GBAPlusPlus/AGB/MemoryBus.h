#pragma once
#include <array>
#include <string>
#include <vector>

#include "APU.h"
#include "Flash.h"
#include "Input.h"
#include "PPU.h"
#include "RTC.h"

class MemoryBus
{
public:
    MemoryBus();
    ~MemoryBus() = default;
    
    uint8_t read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);

    void write32(uint32_t address, uint32_t value);
    void write16(uint32_t address, uint16_t value);
    void write8(uint32_t address, uint8_t value);

    void loadBIOS(const uint8_t* data, size_t size);
    void loadROM(const uint8_t* data, size_t size);
    void unloadROM();

    void reset();

    void TickPPU();
    
    uint8_t TickTimers();

    static constexpr uint32_t FIFO_A_ADDRESS = 0x040000A0;
    static constexpr uint32_t FIFO_B_ADDRESS = 0x040000A4;

    APU& GetAPU();

    void RenderFrame(uint32_t* pixels);

    void DumpDebugState(const std::string& path);

    void SaveFrameAsBMP(const std::string& path);

    bool IsHalted() const;
    void ClearHalt();

    Input& GetInput();
    Flash& GetSaveChip();

    uint32_t ConsumeCycles();
    
    uint8_t read8Raw(uint32_t address);
    uint16_t read16Raw(uint32_t address);
    uint32_t read32Raw(uint32_t address);
    void write8Raw(uint32_t address, uint8_t value);
    void write16Raw(uint32_t address, uint16_t value);
    void write32Raw(uint32_t address, uint32_t value);

private:
    std::array<uint8_t, 256 * 1024> bios;
    std::array<uint8_t, 256 * 1024> ewram;
    std::array<uint8_t, 32 * 1024> iwram;
    std::array<uint8_t, 1024> ioRegisters;
    std::array<uint8_t, 1024> paletteRAM;
    std::array<uint8_t, 96 * 1024> vram;
    std::array<uint8_t, 1024> oam;
    std::vector<uint8_t> rom;

    uint32_t lastRead;
    bool biosLocked;
    bool halted;

    uint32_t pendingCycles = 0;
    void AddAccessCycles(uint32_t address, uint32_t width);

    struct DmaChannel
    {
        uint32_t source = 0;
        uint32_t destination = 0;
        uint16_t count = 0;
        uint16_t control = 0;
        bool armed = false;
    };
    std::array<DmaChannel, 4> dma;

    void OnDmaControlWrite(int channel);
    void RunDma(int channel);
    void TriggerDmaChannels(uint8_t startTiming);
    void TriggerSoundFifoDma(uint32_t fifoAddress);

    struct TimerChannel
    {
        uint16_t reload = 0;
        uint16_t counter = 0;
        uint8_t control = 0;
        uint32_t prescalerCounter = 0;
        bool running = false;
    };
    std::array<TimerChannel, 4> timers;

    void OnTimerControlWrite(int index);
    void OnTimerReloadWrite(int index);

    void OnSiocntWrite();

    PPU ppu;

    APU apu;

    RTC rtc;
    static bool IsGpioOffset(uint32_t romOffset);

    uint8_t readIO(uint32_t offset);
    void writeIO(uint32_t offset, uint8_t value);
    
    uint8_t readVRAM(uint32_t address);
    void writeVRAM(uint32_t address, uint8_t value);
    
    uint8_t readROM(uint32_t address);

    //FLASH!
    Flash saveChip;

    //buttons
    Input input;

    uint8_t openBusRead();

    inline uint16_t read16Aligned(uint32_t address);
    inline uint32_t read32Aligned(uint32_t address);
    inline void write16Aligned(uint32_t address, uint16_t value);
    inline void write32Aligned(uint32_t address, uint32_t value);
};
