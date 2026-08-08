#pragma once
#include <array>
#include <cstdint>

class PPU
{
public:
    PPU(std::array<uint8_t, 1024>& ioRegisters,
        std::array<uint8_t, 96 * 1024>& vram,
        std::array<uint8_t, 1024>& paletteRAM,
        std::array<uint8_t, 1024>& oam);

    void Reset();

    struct TickResult
    {
        bool vblankStarted = false;
        bool hblankStarted = false;
    };

    TickResult Tick();

    void RenderFrame(uint32_t* pixels);

private:
    std::array<uint32_t, 240 * 160> latchedFrame{};
    void ComposeScanline(int screenY);

    static uint32_t Bgr555ToArgb(uint16_t color);
    uint32_t PaletteColor(bool obj, int index);
    bool SampleRegularBackground(int bgIndex, int screenX, int screenY, uint32_t& outColor);
    bool SampleAffineBackground(int bgIndex, int screenX, int screenY, uint32_t& outColor);
    bool SampleBitmapMode3(int screenX, int screenY, uint32_t& outColor);
    bool SampleBitmapMode4(int screenX, int screenY, uint16_t dispcnt, uint32_t& outColor);
    bool SampleBitmapMode5(int screenX, int screenY, uint16_t dispcnt, uint32_t& outColor);

    //im ngl i dont know why i made this a struct, i think it was like easier or something.... idk
    struct SpritePixel
    {
        uint32_t color = 0;
        uint8_t priority = 0;
        bool opaque = false;
        bool semiTransparent = false;
    };
    void BuildSpriteLine(int screenY);

    uint8_t GetWindowLayerMask(int x, int y);
    bool PointInWindow(int index, int x, int y);

    enum LayerId : uint8_t
    {
        LAYER_BG0 = 0,
        LAYER_BG1 = 1,
        LAYER_BG2 = 2,
        LAYER_BG3 = 3,
        LAYER_OBJ = 4,
        LAYER_BACKDROP = 5
    };
    
    struct PixelStack
    {
        uint32_t topColor = 0;
        uint32_t secondColor = 0;
        uint8_t topLayer = LAYER_BACKDROP;
        uint8_t secondLayer = LAYER_BACKDROP;
        bool topSemiTransparent = false;
    };
    std::array<PixelStack, 240> pixelLine;

    void ResetPixelLine(uint32_t backdrop);
    void PushPixel(int x, uint32_t color, uint8_t layer, bool semiTransparent = false);

    void ApplyColorEffects(int screenY);

    std::array<uint8_t, 1024>& ioRegisters;
    std::array<uint8_t, 96 * 1024>& vram;
    std::array<uint8_t, 1024>& paletteRAM;
    std::array<uint8_t, 1024>& oam;

    std::array<SpritePixel, 240> spriteLine;
    std::array<bool, 240> objWindowLine;

    uint32_t ppuCycleCounter = 0;
};
