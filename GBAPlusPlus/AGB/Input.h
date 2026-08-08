#pragma once
#include <atomic>
#include <cstdint>

//the buttons!!!!!!!
enum class GbaButton : uint8_t
{
    A      = 0,
    B      = 1,
    Select = 2,
    Start  = 3,
    Right  = 4,
    Left   = 5,
    Up     = 6,
    Down   = 7,
    R      = 8,
    L      = 9
};


class Input
{
public:
    Input();

    void Reset();

    void SetButton(GbaButton button, bool pressed);
    void SetPressedMask(uint16_t mask);
    void ReleaseAll();
    uint16_t GetPressedMask() const;

    //offset is relative to the io base
    uint8_t ReadRegister(uint32_t offset) const;
    void WriteRegister(uint32_t offset, uint8_t value);

    static bool IsInputRegister(uint32_t offset);

    bool ConsumeIrqRequest();

    static constexpr uint32_t KEYINPUT_OFFSET = 0x130;
    static constexpr uint32_t KEYCNT_OFFSET   = 0x132;

private:
    static constexpr uint16_t BUTTON_MASK = 0x03FF;

    static constexpr uint16_t IRQ_ENABLE_BIT = 0x4000;
    static constexpr uint16_t IRQ_AND_MODE_BIT = 0x8000;

    std::atomic<uint16_t> pressed{0};
    uint16_t control = 0;
    bool irqConditionWasMet = false;

    bool IsIrqConditionMet() const;
};
