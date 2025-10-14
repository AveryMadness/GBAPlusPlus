#pragma once
#include <array>
#include <vector>

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

    void reset();
    
private:
    std::array<uint8_t, 256 * 1024> bios;
    std::array<uint8_t, 256 * 1024> ewram;
    std::array<uint8_t, 32 * 1024> iwram;
    std::array<uint8_t, 1024> ioRegisters;
    std::array<uint8_t, 1024> paletteRAM;
    std::array<uint8_t, 96 * 1024> vram;
    std::array<uint8_t, 1024> oam;
    std::vector<uint8_t> rom;
    std::array<uint8_t, 65536> sram;

    uint32_t lastRead;                            
    bool biosLocked;
    
    uint8_t readIO(uint32_t offset);
    void writeIO(uint32_t offset, uint8_t value);
    
    uint8_t readVRAM(uint32_t address);
    void writeVRAM(uint32_t address, uint8_t value);
    
    uint8_t readROM(uint32_t address);
    uint8_t readSaveMemory(uint32_t address);
    void writeSaveMemory(uint32_t address, uint8_t value);
    
    uint8_t openBusRead();

    inline uint16_t read16Aligned(uint32_t address);
    inline uint32_t read32Aligned(uint32_t address);
    inline void write16Aligned(uint32_t address, uint16_t value);
    inline void write32Aligned(uint32_t address, uint32_t value);
};
