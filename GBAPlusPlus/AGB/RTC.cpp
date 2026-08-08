#include "RTC.h"
#include <cstring>
#include <ctime>

namespace
{
    uint8_t ToBCD(int value)
    {
        return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
    }
}

RTC::RTC()
{
    Reset();
}

void RTC::Reset()
{
    gpioData = 0;
    gpioDirection = 0;
    gpioReadEnable = 0;
    phase = Phase::Idle;
    bitCount = 0;
    shiftRegister = 0;
    activeCommand = 0;
    activeRegister = 0;
    std::memset(dataBuffer, 0, sizeof(dataBuffer));
    dataLength = 0;
    statusRegister = 0x40;
}

bool RTC::IsReadEnabled() const
{
    return (gpioReadEnable & 0x1) != 0;
}

uint8_t RTC::ReadRegister(uint32_t address) const
{
    uint32_t offset = address & 0xF;
    
    if (offset == 0x5 || offset == 0x7 || offset == 0x9)
        return 0;

    if (offset == 0x4)
    {
        uint8_t liveValue = gpioData & gpioDirection;
        if (!(gpioDirection & PIN_SIO))
            liveValue |= ComputeSioOutBit() ? PIN_SIO : 0;
        return liveValue;
    }
    if (offset == 0x6)
        return gpioDirection;
    if (offset == 0x8)
        return gpioReadEnable;
    return 0;
}

void RTC::WriteRegister(uint32_t address, uint8_t value)
{
    uint32_t offset = address & 0xF;
    
    if (offset == 0x5 || offset == 0x7 || offset == 0x9)
        return;

    if (offset == 0x4)
    {
        uint8_t oldPins = gpioData;
        uint8_t newPins = value & 0x7;
        gpioData = newPins;

        bool oldCS = (oldPins & PIN_CS) != 0;
        bool newCS = (newPins & PIN_CS) != 0;
        bool oldSCK = (oldPins & PIN_SCK) != 0;
        bool newSCK = (newPins & PIN_SCK) != 0;

        if (!oldCS && newCS)
            BeginCommandPhase();
        else if (oldCS && !newCS)
            phase = Phase::Idle;
        else if (newCS && !oldSCK && newSCK)
            OnRisingClock();
    }
    else if (offset == 0x6)
    {
        gpioDirection = value & 0x7;
    }
    else if (offset == 0x8)
    {
        gpioReadEnable = value & 0x1;
    }
}

void RTC::BeginCommandPhase()
{
    phase = Phase::ReceivingCommand;
    bitCount = 0;
    shiftRegister = 0;
}

void RTC::OnRisingClock()
{
    switch (phase)
    {
        case Phase::ReceivingCommand:
        {
            bool bit = (gpioData & PIN_SIO) != 0;
            shiftRegister = static_cast<uint8_t>((shiftRegister << 1) | (bit ? 1 : 0));
            bitCount++;
            if (bitCount >= 8)
            {
                activeCommand = shiftRegister;
                OnCommandByteComplete();
            }
            break;
        }

        case Phase::ReceivingData:
        {
            bool bit = (gpioData & PIN_SIO) != 0;
            int byteIndex = bitCount / 8;
            int bitIndex = bitCount % 8;
            if (byteIndex < dataLength && byteIndex < 7)
            {
                dataBuffer[byteIndex] = static_cast<uint8_t>(
                    (dataBuffer[byteIndex] & ~(1 << bitIndex)) | ((bit ? 1 : 0) << bitIndex));
            }
            bitCount++;
            if (bitCount >= dataLength * 8)
            {
                if (activeRegister == 1 && dataLength >= 1)
                    statusRegister = dataBuffer[0];
                phase = Phase::Done;
            }
            break;
        }

        case Phase::SendingData:
        {
            //this edge reveals the bit at (bitCount) once incremented, i.e. bitCount-1
            bitCount++;
            if (bitCount >= dataLength * 8)
                phase = Phase::Done;
            break;
        }

        default:
            break;
    }
}

void RTC::OnCommandByteComplete()
{
    uint8_t reg = (activeCommand >> 1) & 0x7;
    bool isRead = (activeCommand & 0x1) != 0;
    activeRegister = reg;
    bitCount = 0;

    switch (reg)
    {
        case 0: //reset
            statusRegister = 0x40;
            phase = Phase::Idle;
            break;

        case 1: //status/control register, 1 byte
            dataLength = 1;
            if (isRead)
            {
                dataBuffer[0] = statusRegister;
                phase = Phase::SendingData;
            }
            else
            {
                phase = Phase::ReceivingData;
            }
            break;

        case 2: //full date+time, 7 bytes: year,month,day,weekday,hour,minute,second (BCD)
            dataLength = 7;
            if (isRead)
            {
                LatchCurrentDateTime(dataBuffer, 7);
                phase = Phase::SendingData;
            }
            else
            {
                phase = Phase::ReceivingData;
            }
            break;

        case 3: //time only, 3 bytes: hour,minute,second (BCD)
            dataLength = 3;
            if (isRead)
            {
                uint8_t full[7];
                LatchCurrentDateTime(full, 7);
                dataBuffer[0] = full[4];
                dataBuffer[1] = full[5];
                dataBuffer[2] = full[6];
                phase = Phase::SendingData;
            }
            else
            {
                phase = Phase::ReceivingData;
            }
            break;

        default:
            dataLength = 1;
            if (isRead)
            {
                dataBuffer[0] = 0;
                phase = Phase::SendingData;
            }
            else
            {
                phase = Phase::ReceivingData;
            }
            break;
    }
}

uint8_t RTC::ComputeSioOutBit() const
{
    if (phase != Phase::SendingData || bitCount < 1)
        return 0;

    int revealedBit = bitCount - 1;
    int byteIndex = revealedBit / 8;
    int bitIndex = revealedBit % 8;
    if (byteIndex >= dataLength || byteIndex >= 7)
        return 0;

    return (dataBuffer[byteIndex] >> bitIndex) & 0x1;
}

void RTC::LatchCurrentDateTime(uint8_t* out, int count)
{
    //I LOVE DATE TIME!!!!!
    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);

    uint8_t full[7] = {
        ToBCD(local.tm_year % 100),
        ToBCD(local.tm_mon + 1),
        ToBCD(local.tm_mday),
        ToBCD(local.tm_wday),
        ToBCD(local.tm_hour),
        ToBCD(local.tm_min),
        ToBCD(local.tm_sec)
    };

    for (int i = 0; i < count && i < 7; i++)
        out[i] = full[i];
}
