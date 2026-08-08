#include "Input.h"

Input::Input()
{
    Reset();
}

void Input::Reset()
{
    control = 0;
    irqConditionWasMet = false;
}

void Input::SetButton(GbaButton button, bool isPressed)
{
    const uint16_t bit = static_cast<uint16_t>(1u << static_cast<uint8_t>(button));

    uint16_t current = pressed.load(std::memory_order_relaxed);
    uint16_t updated;
    do
    {
        updated = isPressed ? (current | bit) : static_cast<uint16_t>(current & ~bit);
    }
    while (!pressed.compare_exchange_weak(current, updated, std::memory_order_relaxed));
}

void Input::SetPressedMask(uint16_t mask)
{
    pressed.store(mask & BUTTON_MASK, std::memory_order_relaxed);
}

void Input::ReleaseAll()
{
    pressed.store(0, std::memory_order_relaxed);
}

uint16_t Input::GetPressedMask() const
{
    return pressed.load(std::memory_order_relaxed);
}

bool Input::IsInputRegister(uint32_t offset)
{
    return offset >= KEYINPUT_OFFSET && offset < KEYCNT_OFFSET + 2;
}

uint8_t Input::ReadRegister(uint32_t offset) const
{
    //active low
    const uint16_t keyInput = static_cast<uint16_t>(~pressed.load(std::memory_order_relaxed) & BUTTON_MASK);

    switch (offset)
    {
        case KEYINPUT_OFFSET:     return static_cast<uint8_t>(keyInput & 0xFF);
        case KEYINPUT_OFFSET + 1: return static_cast<uint8_t>(keyInput >> 8);
        case KEYCNT_OFFSET:       return static_cast<uint8_t>(control & 0xFF);
        case KEYCNT_OFFSET + 1:   return static_cast<uint8_t>(control >> 8);
        default:                  return 0;
    }
}

void Input::WriteRegister(uint32_t offset, uint8_t value)
{
    //no writing... how would that even work
    if (offset == KEYINPUT_OFFSET || offset == KEYINPUT_OFFSET + 1)
        return;

    if (offset == KEYCNT_OFFSET)
        control = static_cast<uint16_t>((control & 0xFF00) | value);
    else if (offset == KEYCNT_OFFSET + 1)
        control = static_cast<uint16_t>((control & 0x00FF) | (value << 8));
}

bool Input::IsIrqConditionMet() const
{
    if ((control & IRQ_ENABLE_BIT) == 0)
        return false;

    const uint16_t watched = control & BUTTON_MASK;
    if (watched == 0)
        return false;

    const uint16_t held = pressed.load(std::memory_order_relaxed) & BUTTON_MASK;

    //bit 15 clear asks for any watched button, set asks for all of them at once
    if (control & IRQ_AND_MODE_BIT)
        return (held & watched) == watched;

    return (held & watched) != 0;
}

bool Input::ConsumeIrqRequest()
{
    if ((control & IRQ_ENABLE_BIT) == 0)
    {
        irqConditionWasMet = false;
        return false;
    }

    const bool met = IsIrqConditionMet();
    const bool rising = met && !irqConditionWasMet;
    irqConditionWasMet = met;
    return rising;
}
