#include "MemoryBus.h"

#include <SDL3/SDL_haptic.h>

MemoryBus::MemoryBus()
    : lastRead(0)
    , biosLocked(false)
{
    reset();
}

void MemoryBus::reset()
{
    bios.fill(0);
    ewram.fill(0);
    iwram.fill(0);
    ioRegisters.fill(0);
    paletteRAM.fill(0);
    vram.fill(0);
    oam.fill(0);
    sram.fill(0xFF); //filled with 0xFF for uninitialized
    lastRead = 0;
    biosLocked = false;

    ioRegisters[0x000] = 0x80; //DISPCNT
    ioRegisters[0x004] = 0x00; //DISPSTAT
    ioRegisters[0x130] = 0xFF; //key input - not pressed
    ioRegisters[0x131] = 0x03; //key input - high byte?
}

void MemoryBus::loadBIOS(const uint8_t* data, size_t size)
{
    size_t copySize = size > bios.size() ? bios.size() : size;
    std::memcpy(bios.data(), data, copySize);
}

void MemoryBus::loadROM(const uint8_t* data, size_t size)
{
    rom.resize(size);
    std::memcpy(rom.data(), data, size);
}

uint8_t MemoryBus::read8(uint32_t address)
{
    uint8_t memoryRegion = address >> 24;

    switch (memoryRegion)
    {
        case 0x00:
            if (address < 0x4000 && !biosLocked)
            {
                lastRead = bios[address];
                return bios[address];
            }
            return openBusRead();
        
        case 0x02:
            lastRead = ewram[address & 0x3FFFF];
            return ewram[address & 0x3FFFF];
        
        case 0x03:
            lastRead = iwram[address & 0x7FFF];
            return iwram[address & 0x7FFF];
        
        case 0x04:
            if ((address & 0xFFFFFF) < 0x400)
            {
                return readIO(address & 0x3FF);
            }
            return openBusRead();
        
        case 0x05:
            lastRead = paletteRAM[address & 0x3FF];
            return paletteRAM[address & 0x3FF];
        
        case 0x06:
            return readVRAM(address);
            
        case 0x07:
            lastRead = oam[address & 0x3FF];
            return oam[address & 0x3FF];
        
        case 0x08: case 0x09:
        case 0x0A: case 0x0B:
        case 0x0C: case 0x0D:
            return readROM(address);
        
        case 0x0E: case 0x0F:
            return readSaveMemory(address);
        
        default:
            return openBusRead();
    }
}

uint16_t MemoryBus::read16(uint32_t address)
{
    if (address & 1)
    {
        uint16_t value = read16Aligned(address & ~1);
        return (value >> 8) | (value << 8);
    }

    return read16Aligned(address);
}

uint32_t MemoryBus::read32(uint32_t address)
{
    if (address & 3)
    {
        uint32_t value = read32Aligned(address & ~3);
        int rotation = (address & 3) * 8;
        return (value >> rotation) | (value << (32 - rotation));
    }

    return read32Aligned(address);
}

uint16_t MemoryBus::read16Aligned(uint32_t address)
{
    uint8_t memoryRegion = address >> 24;

    switch (memoryRegion)
    {
    case 0x02:
        {
            uint32_t offset = address & 0x3FFFF;
            return *reinterpret_cast<uint16_t*>(&ewram[offset]);
        }
    case 0x03:
        {
            uint32_t offset = address & 0x7FFF;
            return *reinterpret_cast<uint16_t*>(&iwram[offset]);
        }
    case 0x05:
        {
            uint32_t offset = address & 0x3FF;
            return *reinterpret_cast<uint16_t*>(&paletteRAM[offset]);
        }
    case 0x06:
        {
            uint32_t offset = (address & 0x1FFFF);
            if (offset >= 0x18000) offset -= 0x8000;
            return *reinterpret_cast<uint16_t*>(&vram[offset]);
        }
    default:
        return read8(address) | (read8(address + 1) << 8);
    }
}

uint32_t MemoryBus::read32Aligned(uint32_t address)
{
    uint8_t region = address >> 24;

    switch (region)
    {
    case 0x02:
        {
            uint32_t offset = address & 0x3FFFF;
            return *reinterpret_cast<uint32_t*>(&ewram[offset]);
        }
    case 0x03:
        {
            uint32_t offset = address & 0x7FFF;
            return *reinterpret_cast<uint32_t*>(&iwram[offset]);
        }
    default:
        return read16Aligned(address) | (read16Aligned(address + 2) << 16);
    }
}

void MemoryBus::write8(uint32_t address, uint8_t value)
{
    switch (address >> 24) {
    case 0x02:
        ewram[address & 0x3FFFF] = value;
        break;
            
    case 0x03:
        iwram[address & 0x7FFF] = value;
        break;
            
    case 0x04:
        if ((address & 0xFFFFFF) < 0x400) {
            writeIO(address & 0x3FF, value);
        }
        break;
            
    case 0x05: 
        break;
            
    case 0x06: 
        writeVRAM(address, value);
        break;
            
    case 0x07: 
        break;
            
    case 0x0E: case 0x0F:
        writeSaveMemory(address, value);
        break;
            
    default:
        break;
    }
}

void MemoryBus::write16(uint32_t address, uint16_t value) {
    address &= ~1;
    
    write16Aligned(address, value);
}

void MemoryBus::write32(uint32_t address, uint32_t value) {
    address &= ~3;
    
    write32Aligned(address, value);
}

inline void MemoryBus::write16Aligned(uint32_t address, uint16_t value)
{
    switch (address >> 24)
    {
        case 0x02:
            *reinterpret_cast<uint16_t*>(&ewram[address & 0x3FFFF]) = value;
            break;
            
        case 0x03: 
            *reinterpret_cast<uint16_t*>(&iwram[address & 0x7FFF]) = value;
            break;
            
        case 0x04: 
            if ((address & 0xFFFFFF) < 0x400)
            {
                writeIO(address & 0x3FF, value & 0xFF);
                writeIO((address & 0x3FF) + 1, value >> 8);
            }
            break;
            
        case 0x05: 
            *reinterpret_cast<uint16_t*>(&paletteRAM[address & 0x3FF]) = value;
            break;
            
        case 0x06:
        { 
            uint32_t offset = (address & 0x1FFFF);
            if (offset >= 0x18000) offset -= 0x8000;
            *reinterpret_cast<uint16_t*>(&vram[offset]) = value;
            break;
        }
            
        case 0x07:
            *reinterpret_cast<uint16_t*>(&oam[address & 0x3FF]) = value;
            break;
            
        default:
            write8(address, value & 0xFF);
            write8(address + 1, value >> 8);
            break;
    }
}

inline void MemoryBus::write32Aligned(uint32_t address, uint32_t value)
{
    switch (address >> 24)
    {
        case 0x02: 
            *reinterpret_cast<uint32_t*>(&ewram[address & 0x3FFFF]) = value;
            break;
            
        case 0x03: 
            *reinterpret_cast<uint32_t*>(&iwram[address & 0x7FFF]) = value;
            break;
            
        default:
            write16Aligned(address, value & 0xFFFF);
            write16Aligned(address + 2, value >> 16);
            break;
    }
}

uint8_t MemoryBus::readIO(uint32_t offset)
{
    return ioRegisters[offset];
}

void MemoryBus::writeIO(uint32_t offset, uint8_t value)
{
    ioRegisters[offset] = value;
}

uint8_t MemoryBus::readVRAM(uint32_t address)
{
    uint32_t offset = address & 0x1FFFF;

    if (offset >= 0x18000) offset -= 0x8000;

    lastRead = vram[offset];
    return vram[offset];
}

void MemoryBus::writeVRAM(uint32_t address, uint8_t value)
{
    uint32_t offset = address & 0x1FFFF;

    if (offset >= 0x18000) offset -= 0x8000;

    vram[offset] = value;   
}

uint8_t MemoryBus::readROM(uint32_t address)
{
    uint32_t offset = address & 0x1FFFFFF;

    if (offset < rom.size())
    {
        lastRead = rom[offset];
        return rom[offset];
    }

    return openBusRead();
}

uint8_t MemoryBus::readSaveMemory(uint32_t address)
{
    uint32_t offset = address & 0xFFFF;
    return sram[offset];
}

void MemoryBus::writeSaveMemory(uint32_t address, uint8_t value)
{
    uint32_t offset = address & 0xFFFF;
    sram[offset] = value;
}

uint8_t MemoryBus::openBusRead()
{
    return lastRead & 0xFF;
}






