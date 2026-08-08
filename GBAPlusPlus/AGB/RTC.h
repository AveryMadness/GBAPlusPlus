#pragma once
#include <cstdint>

//Seiko S-3511A or something
class RTC
{
public:
    RTC();

    void Reset();

    uint8_t ReadRegister(uint32_t address) const;
    void WriteRegister(uint32_t address, uint8_t value);

    bool IsReadEnabled() const;

private:
    static constexpr uint8_t PIN_SCK = 0x1;
    static constexpr uint8_t PIN_SIO = 0x2;
    static constexpr uint8_t PIN_CS  = 0x4;

    uint8_t gpioData = 0;
    uint8_t gpioDirection = 0;
    uint8_t gpioReadEnable = 0;

    enum class Phase
    {
        Idle,
        ReceivingCommand,
        SendingData,
        ReceivingData,
        Done
    };
    Phase phase = Phase::Idle;

    int bitCount = 0;

    uint8_t shiftRegister = 0;
    uint8_t activeCommand = 0;
    uint8_t activeRegister = 0;

    uint8_t dataBuffer[7] = {};
    int dataLength = 0;

    uint8_t statusRegister = 0x40;

    void BeginCommandPhase();
    void OnRisingClock();
    void OnCommandByteComplete();
    uint8_t ComputeSioOutBit() const;
    static void LatchCurrentDateTime(uint8_t* out, int count);
};
