#include "Flash.h"

Flash::Flash()
{
    Reset();
}

void Flash::Reset()
{
    data.fill(0xFF);

    commandPhase = 0;
    idMode = false;
    erasePrepared = false;
    writePending = false;
    bankSwitchPending = false;
    bank = 0;
    dirty = false;
}

const uint8_t* Flash::Data() const
{
    return data.data();
}

uint8_t* Flash::Data()
{
    return data.data();
}

size_t Flash::Size() const
{
    return data.size();
}

bool Flash::ConsumeDirty()
{
    const bool was = dirty;
    dirty = false;
    return was;
}

uint8_t Flash::Read(uint32_t address) const
{
    const uint32_t offset = address & 0xFFFF;

    if (idMode)
    {
        if (offset == 0)
            return MANUFACTURER_ID;
        if (offset == 1)
            return DEVICE_ID;
    }

    return data[bank * BANK_SIZE + offset];
}

void Flash::Write(uint32_t address, uint8_t value)
{
    const uint32_t offset = address & 0xFFFF;

    //a write armed by 0xA0 consumes the very next write, wherever it lands
    if (writePending)
    {
        writePending = false;
        data[bank * BANK_SIZE + offset] = value;
        dirty = true;
        return;
    }

    //a bank switch armed by 0xB0 consumes the next write
    if (bankSwitchPending)
    {
        bankSwitchPending = false;
        bank = value & 1;
        return;
    }

    if (commandPhase == 0 && offset == 0x5555 && value == 0xAA)
    {
        commandPhase = 1;
        return;
    }

    if (commandPhase == 1 && offset == 0x2AAA && value == 0x55)
    {
        commandPhase = 2;
        return;
    }

    if (commandPhase == 2)
    {
        commandPhase = 0;
        HandleCommand(offset, value);
        return;
    }

    //anything outside the unlock sequence is an ordinary save-data write
    commandPhase = 0;
    data[bank * BANK_SIZE + offset] = value;
    dirty = true;
}

void Flash::HandleCommand(uint32_t offset, uint8_t value)
{
    //sector erase
    if (erasePrepared && value == 0x30)
    {
        erasePrepared = false;

        const uint32_t sector = bank * BANK_SIZE + (offset & 0xF000);
        for (uint32_t i = 0; i < SECTOR_SIZE; i++)
            data[sector + i] = 0xFF;

        dirty = true;
        return;
    }

    if (offset != 0x5555)
        return;

    switch (value)
    {
        //enter software ID mode
        case 0x90:
            idMode = true;
            break;

        //leave software ID mode
        case 0xF0:
            idMode = false;
            break;

        //erase prefix
        case 0x80:
            erasePrepared = true;
            break;

        //erase the whole chip
        case 0x10:
            if (erasePrepared)
            {
                erasePrepared = false;
                data.fill(0xFF);
                dirty = true;
            }
            break;

        //arm a single byte write
        case 0xA0:
            writePending = true;
            break;

        //arm a bank switch
        case 0xB0:
            bankSwitchPending = true;
            break;

        default:
            break;
    }
}
