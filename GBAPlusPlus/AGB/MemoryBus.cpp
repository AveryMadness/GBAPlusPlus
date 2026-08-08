#include "MemoryBus.h"

#include <fstream>
#include <SDL3/SDL_haptic.h>

MemoryBus::MemoryBus()
    : lastRead(0)
    , biosLocked(false)
    , halted(false)
    , ppu(ioRegisters, vram, paletteRAM, oam)
{
    bios.fill(0);
    reset();
}

void MemoryBus::reset()
{
    //dont reset bios....
    ewram.fill(0);
    iwram.fill(0);
    ioRegisters.fill(0);
    paletteRAM.fill(0);
    vram.fill(0);
    oam.fill(0);
    saveChip.Reset();
    lastRead = 0;
    biosLocked = false;
    halted = false;
    dma.fill(DmaChannel{});
    timers.fill(TimerChannel{});
    ppu.Reset();
    rtc.Reset();
    input.Reset();

    //SOUNDBIAS, resets to 0x0200
    ioRegisters[0x088] = 0x00;
    ioRegisters[0x089] = 0x02;

    //SIOMULTI0-3 says NO PLAYERS!
    for (uint32_t i = 0x120; i < 0x128; i++)
        ioRegisters[i] = 0xFF;
}

void MemoryBus::TickPPU()
{
    PPU::TickResult result = ppu.Tick();

    if (result.vblankStarted)
        TriggerDmaChannels(1);

    //dont run during vblank
    if (result.hblankStarted && ioRegisters[0x006] < 160)
        TriggerDmaChannels(2);
}

void MemoryBus::OnDmaControlWrite(int channel)
{
    static constexpr uint32_t sadOffsets[4]  = {0xB0, 0xBC, 0xC8, 0xD4};
    static constexpr uint32_t dadOffsets[4]  = {0xB4, 0xC0, 0xCC, 0xD8};
    static constexpr uint32_t cntLOffsets[4] = {0xB8, 0xC4, 0xD0, 0xDC};
    static constexpr uint32_t cntHOffsets[4] = {0xBA, 0xC6, 0xD2, 0xDE};

    uint16_t control = static_cast<uint16_t>(ioRegisters[cntHOffsets[channel]]
        | (ioRegisters[cntHOffsets[channel] + 1] << 8));

    //enable bit not set, nothing to arm
    if (!(control & 0x8000))
        return;

    DmaChannel& ch = dma[channel];
    ch.source = *reinterpret_cast<uint32_t*>(&ioRegisters[sadOffsets[channel]]) & 0x0FFFFFFF;
    ch.destination = *reinterpret_cast<uint32_t*>(&ioRegisters[dadOffsets[channel]])
        & (channel == 3 ? 0x0FFFFFFF : 0x07FFFFFF);
    ch.count = *reinterpret_cast<uint16_t*>(&ioRegisters[cntLOffsets[channel]]);
    ch.control = control;

    uint8_t startTiming = (control >> 12) & 0x3;
    if (startTiming == 0)
        RunDma(channel);
    else
        ch.armed = true;
}

void MemoryBus::TriggerDmaChannels(uint8_t startTiming)
{
    for (int i = 0; i < 4; i++)
    {
        if (dma[i].armed && ((dma[i].control >> 12) & 0x3) == startTiming)
            RunDma(i);
    }
}

void MemoryBus::RunDma(int channel)
{
    static constexpr uint32_t cntHOffsets[4] = {0xBA, 0xC6, 0xD2, 0xDE};

    DmaChannel& ch = dma[channel];

    uint32_t count = ch.count;
    if (count == 0)
        count = (channel == 3) ? 0x10000 : 0x4000;

    bool wordTransfer = (ch.control & 0x0400) != 0;
    uint8_t destControl = (ch.control >> 5) & 0x3;
    uint8_t srcControl = (ch.control >> 7) & 0x3;
    uint32_t step = wordTransfer ? 4 : 2;

    uint32_t source = ch.source;
    uint32_t destination = ch.destination;
    uint32_t destinationStart = destination;

    for (uint32_t i = 0; i < count; i++)
    {
        if (wordTransfer)
            write32Raw(destination, read32Raw(source));
        else
            write16Raw(destination, read16Raw(source));

        switch (srcControl)
        {
            case 0: source += step; break;
            case 1: source -= step; break;
            default: break;
        }

        switch (destControl)
        {
            case 0: case 3: destination += step; break;
            case 1: destination -= step; break;
            default: break;
        }
    }

    bool repeat = (ch.control & 0x0200) != 0;
    uint8_t startTiming = (ch.control >> 12) & 0x3;

    if (repeat && startTiming != 0)
    {
        ch.source = source;

        //increment/reload resets destination for the next trigger
        ch.destination = (destControl == 3) ? destinationStart : destination;
        ch.armed = true;
    }
    else
    {
        ch.armed = false;
        ioRegisters[cntHOffsets[channel] + 1] &= static_cast<uint8_t>(~0x80);
    }

    if (ch.control & 0x4000)
        ioRegisters[0x203] |= static_cast<uint8_t>(1 << channel);
}

void MemoryBus::TickTimers()
{
    static constexpr uint32_t cntLOffsets[4] = {0x100, 0x104, 0x108, 0x10C};
    static constexpr uint32_t prescalerCycles[4] = {1, 64, 256, 1024};
    
    if (input.ConsumeIrqRequest())
        ioRegisters[0x203] |= static_cast<uint8_t>(1 << 4);

    bool previousOverflowed = false;

    for (int i = 0; i < 4; i++)
    {
        TimerChannel& timer = timers[i];
        if (!timer.running)
        {
            previousOverflowed = false;
            continue;
        }

        bool cascade = (i != 0) && (timer.control & 0x4);
        bool tick;

        if (cascade)
        {
            tick = previousOverflowed;
        }
        else
        {
            timer.prescalerCounter++;
            tick = timer.prescalerCounter >= prescalerCycles[timer.control & 0x3];
            if (tick)
                timer.prescalerCounter = 0;
        }

        bool overflowed = false;
        if (tick)
        {
            if (timer.counter == 0xFFFF)
            {
                timer.counter = timer.reload;
                overflowed = true;

                if (timer.control & 0x40)
                    ioRegisters[0x202] |= static_cast<uint8_t>(1 << (3 + i));
            }
            else
            {
                timer.counter++;
            }
        }

        //keep the memory-mapped copy in sync so plain reads see the live count
        ioRegisters[cntLOffsets[i]] = static_cast<uint8_t>(timer.counter & 0xFF);
        ioRegisters[cntLOffsets[i] + 1] = static_cast<uint8_t>(timer.counter >> 8);

        previousOverflowed = overflowed;
    }
}

void MemoryBus::OnTimerReloadWrite(int index)
{
    static constexpr uint32_t cntLOffsets[4] = {0x100, 0x104, 0x108, 0x10C};

    //writes to CNT_L only ever set the reload value, never the live counter
    timers[index].reload = static_cast<uint16_t>(static_cast<uint32_t>(ioRegisters[cntLOffsets[index]])
        | (static_cast<uint32_t>(ioRegisters[cntLOffsets[index] + 1]) << 8));
}

void MemoryBus::OnTimerControlWrite(int index)
{
    static constexpr uint32_t cntHOffsets[4] = {0x102, 0x106, 0x10A, 0x10E};

    uint8_t control = ioRegisters[cntHOffsets[index]];
    bool nowRunning = (control & 0x80) != 0;

    if (nowRunning && !timers[index].running)
    {
        timers[index].counter = timers[index].reload;
        timers[index].prescalerCounter = 0;
    }

    timers[index].control = control;
    timers[index].running = nowRunning;
}

bool MemoryBus::IsHalted() const
{
    return halted;
}

void MemoryBus::ClearHalt()
{
    halted = false;
}

Input& MemoryBus::GetInput()
{
    return input;
}

Flash& MemoryBus::GetSaveChip()
{
    return saveChip;
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

void MemoryBus::unloadROM()
{
    rom.clear();
    rom.shrink_to_fit();
}

uint32_t MemoryBus::ConsumeCycles()
{
    uint32_t cycles = pendingCycles;
    pendingCycles = 0;
    return cycles;
}

void MemoryBus::AddAccessCycles(uint32_t address, uint32_t width)
{
    uint8_t region = address >> 24;
    uint32_t cycles;

    switch (region)
    {
        case 0x00: cycles = 1; break; //BIOS
        case 0x02: cycles = (width == 4) ? 6 : 3; break; //EWRAM, 2 wait states
        case 0x03: cycles = 1; break; //IWRAM
        case 0x04: cycles = 1; break; //I/O
        case 0x05: cycles = (width == 4) ? 2 : 1; break; //palette RAM
        case 0x06: cycles = (width == 4) ? 2 : 1; break; //VRAM
        case 0x07: cycles = 1; break; //OAM
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            cycles = (width == 4) ? 8 : 4; break; //GamePak ROM, wait state 0
        case 0x0E: case 0x0F: cycles = 5; break; //SRAM/Flash
        default: cycles = 1; break; //open bus
    }

    pendingCycles += cycles;
}

uint8_t MemoryBus::read8(uint32_t address)
{
    AddAccessCycles(address, 1);
    return read8Raw(address);
}

uint8_t MemoryBus::read8Raw(uint32_t address)
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
            if (IsGpioOffset(address & 0x1FFFFFF) && rtc.IsReadEnabled())
                return rtc.ReadRegister(address);
            return readROM(address);

        case 0x0E: case 0x0F:
            return saveChip.Read(address);

        default:
            return openBusRead();
    }
}

bool MemoryBus::IsGpioOffset(uint32_t romOffset)
{
    return romOffset >= 0xC4 && romOffset <= 0xC9;
}

uint16_t MemoryBus::read16(uint32_t address)
{
    AddAccessCycles(address, 2);

    if (address & 1)
    {
        uint16_t value = read16Aligned(address & ~1);
        return (value >> 8) | (value << 8);
    }

    return read16Aligned(address);
}

uint32_t MemoryBus::read32(uint32_t address)
{
    AddAccessCycles(address, 4);

    if (address & 3)
    {
        uint32_t value = read32Aligned(address & ~3);
        int rotation = (address & 3) * 8;
        return (value >> rotation) | (value << (32 - rotation));
    }

    return read32Aligned(address);
}

uint16_t MemoryBus::read16Raw(uint32_t address)
{
    if (address & 1)
    {
        uint16_t value = read16Aligned(address & ~1);
        return (value >> 8) | (value << 8);
    }

    return read16Aligned(address);
}

uint32_t MemoryBus::read32Raw(uint32_t address)
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
    case 0x00:
        if (address < 0x4000 && !biosLocked)
            return *reinterpret_cast<uint16_t*>(&bios[address]);
        return read8Raw(address) | (read8Raw(address + 1) << 8);
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
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        {
            uint32_t offset = address & 0x1FFFFFF;
            if (IsGpioOffset(offset) && rtc.IsReadEnabled())
                return static_cast<uint16_t>(rtc.ReadRegister(address) | (rtc.ReadRegister(address + 1) << 8));
            if (offset + 2 <= rom.size())
                return *reinterpret_cast<uint16_t*>(&rom[offset]);
            return read8Raw(address) | (read8Raw(address + 1) << 8);
        }
    default:
        return read8Raw(address) | (read8Raw(address + 1) << 8);
    }
}

uint32_t MemoryBus::read32Aligned(uint32_t address)
{
    uint8_t region = address >> 24;

    switch (region)
    {
    case 0x00:
        if (address < 0x4000 && !biosLocked)
            return *reinterpret_cast<uint32_t*>(&bios[address]);
        return read16Aligned(address) | (read16Aligned(address + 2) << 16);
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
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        {
            uint32_t offset = address & 0x1FFFFFF;
            if (IsGpioOffset(offset) || IsGpioOffset(offset + 2))
                return read16Aligned(address) | (static_cast<uint32_t>(read16Aligned(address + 2)) << 16);
            if (offset + 4 <= rom.size())
                return *reinterpret_cast<uint32_t*>(&rom[offset]);
            return read16Aligned(address) | (read16Aligned(address + 2) << 16);
        }
    default:
        return read16Aligned(address) | (read16Aligned(address + 2) << 16);
    }
}

void MemoryBus::write8(uint32_t address, uint8_t value)
{
    AddAccessCycles(address, 1);
    write8Raw(address, value);
}

void MemoryBus::write8Raw(uint32_t address, uint8_t value)
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
            
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        //can only write to gpio... dont let people write to the rom anymore.. that was bad...
        if (IsGpioOffset(address & 0x1FFFFFF))
            rtc.WriteRegister(address, value);
        break;

    case 0x0E: case 0x0F:
        saveChip.Write(address, value);
        break;

    default:
        break;
    }
}

void MemoryBus::write16(uint32_t address, uint16_t value) {
    AddAccessCycles(address, 2);
    address &= ~1;

    write16Aligned(address, value);
}

void MemoryBus::write32(uint32_t address, uint32_t value) {
    AddAccessCycles(address, 4);
    address &= ~3;

    write32Aligned(address, value);
}

void MemoryBus::write16Raw(uint32_t address, uint16_t value) {
    write16Aligned(address & ~1, value);
}

void MemoryBus::write32Raw(uint32_t address, uint32_t value) {
    write32Aligned(address & ~3, value);
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
            write8Raw(address, value & 0xFF);
            write8Raw(address + 1, value >> 8);
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
    if (Input::IsInputRegister(offset))
        return input.ReadRegister(offset);

    //this shit weird
    if (offset == 0x128 && ((ioRegisters[0x129] >> 4) & 0x3) == 1)
        return static_cast<uint8_t>((ioRegisters[0x128] & ~0x3C) | 0x04);

    return ioRegisters[offset];
}

void MemoryBus::writeIO(uint32_t offset, uint8_t value)
{
    //link cable stuff that doesnt matter yet
    if (offset >= 0x120 && offset < 0x128)
        return;

    if (Input::IsInputRegister(offset))
    {
        input.WriteRegister(offset, value);
        return;
    }
    
    if (offset == 0x004)
    {
        ioRegisters[0x004] = static_cast<uint8_t>((ioRegisters[0x004] & 0x07) | (value & ~0x07));
        return;
    }

    //VCOUNT
    if (offset == 0x006 || offset == 0x007)
        return;
    
    if (offset == 0x202 || offset == 0x203)
    {
        ioRegisters[offset] &= ~value;
        return;
    }

    //HALTCNT
    if (offset == 0x301)
    {
        halted = true;
        return;
    }

    ioRegisters[offset] = value;

    if (offset == 0xBB) OnDmaControlWrite(0);
    else if (offset == 0xC7) OnDmaControlWrite(1);
    else if (offset == 0xD3) OnDmaControlWrite(2);
    else if (offset == 0xDF) OnDmaControlWrite(3);
    else if (offset == 0x100 || offset == 0x101) OnTimerReloadWrite(0);
    else if (offset == 0x104 || offset == 0x105) OnTimerReloadWrite(1);
    else if (offset == 0x108 || offset == 0x109) OnTimerReloadWrite(2);
    else if (offset == 0x10C || offset == 0x10D) OnTimerReloadWrite(3);
    else if (offset == 0x102) OnTimerControlWrite(0);
    else if (offset == 0x106) OnTimerControlWrite(1);
    else if (offset == 0x10A) OnTimerControlWrite(2);
    else if (offset == 0x10E) OnTimerControlWrite(3);
    else if (offset == 0x128 || offset == 0x129) OnSiocntWrite();
}

void MemoryBus::OnSiocntWrite()
{
    //?
    ioRegisters[0x128] &= static_cast<uint8_t>(~0x80);
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
    
    uint16_t openBusValue = static_cast<uint16_t>((address >> 1) & 0xFFFF);
    uint8_t byte = (address & 1) ? static_cast<uint8_t>(openBusValue >> 8) : static_cast<uint8_t>(openBusValue);
    lastRead = byte;
    return byte;
}


uint8_t MemoryBus::openBusRead()
{
    return lastRead & 0xFF;
}

void MemoryBus::RenderFrame(uint32_t* pixels)
{
    ppu.RenderFrame(pixels);
}


//thanks claude
void MemoryBus::DumpDebugState(const std::string& path)
{
    std::ofstream out(path);
    if (!out.is_open())
        return;

    uint16_t dispcnt = static_cast<uint16_t>(ioRegisters[0x000] | (ioRegisters[0x001] << 8));
    uint16_t dispstat = static_cast<uint16_t>(ioRegisters[0x004] | (ioRegisters[0x005] << 8));
    uint16_t win0h = static_cast<uint16_t>(ioRegisters[0x040] | (ioRegisters[0x041] << 8));
    uint16_t win1h = static_cast<uint16_t>(ioRegisters[0x042] | (ioRegisters[0x043] << 8));
    uint16_t win0v = static_cast<uint16_t>(ioRegisters[0x044] | (ioRegisters[0x045] << 8));
    uint16_t win1v = static_cast<uint16_t>(ioRegisters[0x046] | (ioRegisters[0x047] << 8));
    uint16_t winin = static_cast<uint16_t>(ioRegisters[0x048] | (ioRegisters[0x049] << 8));
    uint16_t winout = static_cast<uint16_t>(ioRegisters[0x04A] | (ioRegisters[0x04B] << 8));

    out << std::hex;
    out << "DISPCNT=0x" << dispcnt
        << " mode=" << std::dec << (dispcnt & 0x7) << std::hex
        << " BG0=" << ((dispcnt >> 8) & 1) << " BG1=" << ((dispcnt >> 9) & 1)
        << " BG2=" << ((dispcnt >> 10) & 1) << " BG3=" << ((dispcnt >> 11) & 1)
        << " OBJ=" << ((dispcnt >> 12) & 1)
        << " WIN0=" << ((dispcnt >> 13) & 1) << " WIN1=" << ((dispcnt >> 14) & 1)
        << " OBJWIN=" << ((dispcnt >> 15) & 1) << "\n";
    out << "DISPSTAT=0x" << dispstat << "\n";
    out << "WIN0H=0x" << win0h << " WIN1H=0x" << win1h
        << " WIN0V=0x" << win0v << " WIN1V=0x" << win1v << "\n";
    out << "WININ=0x" << winin << " (win0=0x" << (winin & 0xFF) << " win1=0x" << (winin >> 8) << ")\n";
    out << "WINOUT=0x" << winout << " (outside=0x" << (winout & 0xFF) << " objwin=0x" << (winout >> 8) << ")\n";

    for (int i = 0; i < 4; i++)
    {
        uint16_t bgcnt = static_cast<uint16_t>(ioRegisters[0x08 + i * 2] | (ioRegisters[0x09 + i * 2] << 8));
        out << "BG" << i << "CNT=0x" << std::hex << bgcnt << std::dec << "\n";

        //affine params only exist for BG2/BG3
        if (i == 2 || i == 3)
        {
            uint32_t paramBase = (i == 2) ? 0x20 : 0x30;
            int16_t pa = static_cast<int16_t>(ioRegisters[paramBase] | (ioRegisters[paramBase + 1] << 8));
            int16_t pb = static_cast<int16_t>(ioRegisters[paramBase + 2] | (ioRegisters[paramBase + 3] << 8));
            int16_t pc = static_cast<int16_t>(ioRegisters[paramBase + 4] | (ioRegisters[paramBase + 5] << 8));
            int16_t pd = static_cast<int16_t>(ioRegisters[paramBase + 6] | (ioRegisters[paramBase + 7] << 8));
            uint32_t xRaw = ioRegisters[paramBase + 8] | (ioRegisters[paramBase + 9] << 8)
                | (ioRegisters[paramBase + 10] << 16) | (ioRegisters[paramBase + 11] << 24);
            uint32_t yRaw = ioRegisters[paramBase + 12] | (ioRegisters[paramBase + 13] << 8)
                | (ioRegisters[paramBase + 14] << 16) | (ioRegisters[paramBase + 15] << 24);
            int32_t x0 = static_cast<int32_t>(xRaw << 4) >> 4;
            int32_t y0 = static_cast<int32_t>(yRaw << 4) >> 4;

            uint8_t charBaseBlock = (bgcnt >> 2) & 0x3;
            uint8_t screenBaseBlock = (bgcnt >> 8) & 0x1F;
            out << std::dec
                << "  BG" << i << " affine: PA=" << pa << " PB=" << pb << " PC=" << pc << " PD=" << pd
                << " X=" << x0 << " Y=" << y0
                << " charBase=0x" << std::hex << (charBaseBlock * 0x4000u)
                << " screenBase=0x" << (screenBaseBlock * 0x800u) << std::dec << "\n";

            uint32_t charBase = charBaseBlock * 0x4000u;
            uint32_t screenBase = screenBaseBlock * 0x800u;
            out << "  BG" << i << " first tile bytes (charBase): ";
            for (int b = 0; b < 32 && charBase + b < vram.size(); b++)
                out << std::hex << static_cast<int>(vram[charBase + b]) << " ";
            out << std::dec << "\n";
            out << "  BG" << i << " first tilemap bytes (screenBase): ";
            for (int b = 0; b < 32 && screenBase + b < vram.size(); b++)
                out << std::hex << static_cast<int>(vram[screenBase + b]) << " ";
            out << std::dec << "\n";

            //also count how many non-zero bytes exist in the whole tilemap, to
            //distinguish "empty tilemap" from "just didn't sample bytes 0-31"
            uint32_t nonZeroTilemapBytes = 0;
            for (uint32_t b = 0; b < 0x800 && screenBase + b < vram.size(); b++)
                if (vram[screenBase + b] != 0)
                    nonZeroTilemapBytes++;
            out << "  BG" << i << " non-zero bytes in first 0x800 of tilemap: " << nonZeroTilemapBytes << "\n";
        }
    }
    out << std::dec;

    out << "\nOAM entries (non-zero only):\n";
    for (int i = 0; i < 128; i++)
    {
        uint32_t base = static_cast<uint32_t>(i) * 8;
        uint16_t a0 = static_cast<uint16_t>(oam[base] | (oam[base + 1] << 8));
        uint16_t a1 = static_cast<uint16_t>(oam[base + 2] | (oam[base + 3] << 8));
        uint16_t a2 = static_cast<uint16_t>(oam[base + 4] | (oam[base + 5] << 8));
        if (a0 == 0 && a1 == 0 && a2 == 0)
            continue;

        bool affine = (a0 & 0x100) != 0;
        bool disableOrDouble = (a0 & 0x200) != 0;
        uint8_t objMode = (a0 >> 10) & 0x3;
        uint8_t shape = (a0 >> 14) & 0x3;
        uint8_t size = (a1 >> 14) & 0x3;
        int y = a0 & 0xFF;
        int x = a1 & 0x1FF;
        uint16_t tile = a2 & 0x3FF;
        uint8_t priority = (a2 >> 10) & 0x3;
        uint8_t palette = (a2 >> 12) & 0xF;
        bool is8bpp = (a0 & 0x2000) != 0;

        out << "OBJ[" << i << "] a0=0x" << std::hex << a0 << " a1=0x" << a1 << " a2=0x" << a2 << std::dec
            << " x=" << x << " y=" << y
            << " shape=" << static_cast<int>(shape) << " size=" << static_cast<int>(size)
            << " affine=" << affine << " disableOrDouble=" << disableOrDouble
            << " objMode=" << static_cast<int>(objMode) << " tile=" << tile
            << " prio=" << static_cast<int>(priority) << " pal=" << static_cast<int>(palette)
            << " 8bpp=" << is8bpp << "\n";
    }
}


//thanks claude
void MemoryBus::SaveFrameAsBMP(const std::string& path)
{
    static constexpr int width = 240;
    static constexpr int height = 160;

    std::vector<uint32_t> pixels(width * height);
    RenderFrame(pixels.data());

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
        return;

    int rowSize = (width * 3 + 3) & ~3;
    int dataSize = rowSize * height;
    int fileSize = 14 + 40 + dataSize;

    auto writeLE16 = [&](uint16_t v) { out.put(static_cast<char>(v & 0xFF)); out.put(static_cast<char>((v >> 8) & 0xFF)); };
    auto writeLE32 = [&](uint32_t v) {
        out.put(static_cast<char>(v & 0xFF));
        out.put(static_cast<char>((v >> 8) & 0xFF));
        out.put(static_cast<char>((v >> 16) & 0xFF));
        out.put(static_cast<char>((v >> 24) & 0xFF));
    };

    //BITMAPFILEHEADER
    out.put('B'); out.put('M');
    writeLE32(static_cast<uint32_t>(fileSize));
    writeLE32(0);
    writeLE32(14 + 40);

    //BITMAPINFOHEADER
    writeLE32(40);
    writeLE32(width);
    writeLE32(height); //positive = bottom-up row order
    writeLE16(1);
    writeLE16(24);
    writeLE32(0);
    writeLE32(static_cast<uint32_t>(dataSize));
    writeLE32(2835);
    writeLE32(2835);
    writeLE32(0);
    writeLE32(0);

    std::vector<uint8_t> row(rowSize, 0);
    for (int y = height - 1; y >= 0; y--)
    {
        for (int x = 0; x < width; x++)
        {
            uint32_t p = pixels[y * width + x];
            row[x * 3 + 0] = static_cast<uint8_t>(p & 0xFF);        //B
            row[x * 3 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF); //G
            row[x * 3 + 2] = static_cast<uint8_t>((p >> 16) & 0xFF);//R
        }
        out.write(reinterpret_cast<char*>(row.data()), rowSize);
    }
}
