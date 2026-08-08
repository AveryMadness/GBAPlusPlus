#include "PPU.h"

#include <algorithm>

PPU::PPU(std::array<uint8_t, 1024>& ioRegisters,
    std::array<uint8_t, 96 * 1024>& vram,
    std::array<uint8_t, 1024>& paletteRAM,
    std::array<uint8_t, 1024>& oam)
    : ioRegisters(ioRegisters)
    , vram(vram)
    , paletteRAM(paletteRAM)
    , oam(oam)
{
}

void PPU::Reset()
{
    ppuCycleCounter = 0;

    //DISPCNT
    ioRegisters[0x000] = 0x80;
    //DISPSTAT
    ioRegisters[0x004] = 0x00;
    //VCOUNT
    ioRegisters[0x006] = 0x00; 
}

PPU::TickResult PPU::Tick()
{
    //308 pixels * 4 cycles/pixel
    static constexpr uint32_t CYCLES_PER_SCANLINE = 1232;
    //240 visible pixels * 4 cycles/pixel
    static constexpr uint32_t VISIBLE_CYCLES      = 960;
    static constexpr uint32_t VISIBLE_SCANLINES   = 160;
    static constexpr uint32_t TOTAL_SCANLINES     = 228;

    ppuCycleCounter++;
    if (ppuCycleCounter >= CYCLES_PER_SCANLINE * TOTAL_SCANLINES)
        ppuCycleCounter = 0;

    uint32_t scanline    = ppuCycleCounter / CYCLES_PER_SCANLINE;
    uint32_t cycleInLine = ppuCycleCounter % CYCLES_PER_SCANLINE;

    ioRegisters[0x006] = static_cast<uint8_t>(scanline);
    ioRegisters[0x007] = 0;

    //not line 227
    bool vBlank = scanline >= VISIBLE_SCANLINES && scanline != TOTAL_SCANLINES - 1;
    bool hBlank = cycleInLine >= VISIBLE_CYCLES;
    //vcount match target
    uint8_t vCountTarget = ioRegisters[0x005];
    bool vCounterMatch = (scanline == vCountTarget);

    uint8_t oldDispstat = ioRegisters[0x004];
    uint8_t dispstat = static_cast<uint8_t>((oldDispstat & ~0x07)
        | (vBlank ? 0x01 : 0)
        | (hBlank ? 0x02 : 0)
        | (vCounterMatch ? 0x04 : 0));
    ioRegisters[0x004] = dispstat;

    uint8_t irqBits = 0;
    if (vBlank && !(oldDispstat & 0x01) && (dispstat & 0x08))
        irqBits |= 0x01;
    if (hBlank && !(oldDispstat & 0x02) && (dispstat & 0x10))
        irqBits |= 0x02;
    if (vCounterMatch && !(oldDispstat & 0x04) && (dispstat & 0x20))
        irqBits |= 0x04;

    if (irqBits)
        ioRegisters[0x202] |= irqBits;

    TickResult result;
    result.vblankStarted = vBlank && !(oldDispstat & 0x01);
    result.hblankStarted = hBlank && !(oldDispstat & 0x02);

    //compose the line that just finished drawing, while the state it used is still live
    if (result.hblankStarted && scanline < VISIBLE_SCANLINES)
        ComposeScanline(static_cast<int>(scanline));

    return result;
}

uint32_t PPU::Bgr555ToArgb(uint16_t color)
{
    uint8_t r5 = color & 0x1F;
    uint8_t g5 = (color >> 5) & 0x1F;
    uint8_t b5 = (color >> 10) & 0x1F;

    uint8_t r8 = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    uint8_t g8 = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
    uint8_t b8 = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));

    return 0xFF000000u | (static_cast<uint32_t>(r8) << 16) | (static_cast<uint32_t>(g8) << 8) | b8;
}

uint32_t PPU::PaletteColor(bool obj, int index)
{
    uint32_t addr = (obj ? 0x200 : 0x000) + static_cast<uint32_t>(index) * 2;
    if (addr + 1 >= paletteRAM.size())
        return 0xFF000000u;

    uint16_t color = static_cast<uint16_t>(paletteRAM[addr] | (paletteRAM[addr + 1] << 8));
    return Bgr555ToArgb(color);
}

bool PPU::SampleRegularBackground(int bgIndex, int screenX, int screenY, uint32_t& outColor)
{
    uint32_t bgCntOffset = 0x08 + bgIndex * 2;
    uint16_t bgcnt = static_cast<uint16_t>(ioRegisters[bgCntOffset] | (ioRegisters[bgCntOffset + 1] << 8));

    uint32_t hofsOffset = 0x10 + bgIndex * 4;
    uint32_t vofsOffset = hofsOffset + 2;
    uint16_t hofs = static_cast<uint16_t>((ioRegisters[hofsOffset] | (ioRegisters[hofsOffset + 1] << 8)) & 0x1FF);
    uint16_t vofs = static_cast<uint16_t>((ioRegisters[vofsOffset] | (ioRegisters[vofsOffset + 1] << 8)) & 0x1FF);

    uint8_t sizeMode = (bgcnt >> 14) & 0x3;
    int tilesWide = (sizeMode == 1 || sizeMode == 3) ? 64 : 32;
    int tilesHigh = (sizeMode == 2 || sizeMode == 3) ? 64 : 32;
    int pixelsWide = tilesWide * 8;
    int pixelsHigh = tilesHigh * 8;

    int worldX = (screenX + hofs) % pixelsWide;
    int worldY = (screenY + vofs) % pixelsHigh;

    int tileX = worldX / 8;
    int tileY = worldY / 8;
    int inTileX = worldX % 8;
    int inTileY = worldY % 8;

    //larger BG sizes are built from multiple adjacent 32x32-tile screenblocks
    int screenBlockIndex = 0;
    if (sizeMode == 1 && tileX >= 32) { screenBlockIndex = 1; tileX -= 32; }
    else if (sizeMode == 2 && tileY >= 32) { screenBlockIndex = 1; tileY -= 32; }
    else if (sizeMode == 3)
    {
        int sbx = tileX >= 32 ? 1 : 0;
        int sby = tileY >= 32 ? 1 : 0;
        screenBlockIndex = sby * 2 + sbx;
        tileX -= sbx * 32;
        tileY -= sby * 32;
    }

    uint8_t screenBaseBlock = (bgcnt >> 8) & 0x1F;
    uint32_t entryOffset = (screenBaseBlock + screenBlockIndex) * 0x800u + (tileY * 32 + tileX) * 2u;
    if (entryOffset + 1 >= vram.size())
        return false;

    uint16_t entry = static_cast<uint16_t>(vram[entryOffset] | (vram[entryOffset + 1] << 8));
    uint16_t tileNumber = entry & 0x3FF;
    bool flipX = (entry & 0x400) != 0;
    bool flipY = (entry & 0x800) != 0;
    uint8_t paletteBank = (entry >> 12) & 0xF;

    bool is8bpp = (bgcnt & 0x80) != 0;
    uint8_t charBaseBlock = (bgcnt >> 2) & 0x3;
    uint32_t charBase = charBaseBlock * 0x4000u;

    int sx = flipX ? 7 - inTileX : inTileX;
    int sy = flipY ? 7 - inTileY : inTileY;

    uint8_t colorIndex;
    if (is8bpp)
    {
        uint32_t tileAddr = charBase + tileNumber * 64u + sy * 8u + sx;
        if (tileAddr >= vram.size())
            return false;
        colorIndex = vram[tileAddr];
    }
    else
    {
        uint32_t tileAddr = charBase + tileNumber * 32u + sy * 4u + sx / 2u;
        if (tileAddr >= vram.size())
            return false;
        uint8_t byte = vram[tileAddr];
        colorIndex = (sx & 1) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0xF);
    }

    if (colorIndex == 0)
        return false;

    int paletteIndex = is8bpp ? colorIndex : (paletteBank * 16 + colorIndex);
    outColor = PaletteColor(false, paletteIndex);
    return true;
}

bool PPU::SampleAffineBackground(int bgIndex, int screenX, int screenY, uint32_t& outColor)
{
    uint32_t bgCntOffset = 0x08 + bgIndex * 2;
    uint16_t bgcnt = static_cast<uint16_t>(ioRegisters[bgCntOffset] | (ioRegisters[bgCntOffset + 1] << 8));

    uint32_t paramBase = (bgIndex == 2) ? 0x20 : 0x30;
    int16_t pa = static_cast<int16_t>(ioRegisters[paramBase] | (ioRegisters[paramBase + 1] << 8));
    int16_t pc = static_cast<int16_t>(ioRegisters[paramBase + 4] | (ioRegisters[paramBase + 5] << 8));
    int16_t pb = static_cast<int16_t>(ioRegisters[paramBase + 2] | (ioRegisters[paramBase + 3] << 8));
    int16_t pd = static_cast<int16_t>(ioRegisters[paramBase + 6] | (ioRegisters[paramBase + 7] << 8));

    uint32_t xRaw = ioRegisters[paramBase + 8] | (ioRegisters[paramBase + 9] << 8)
        | (ioRegisters[paramBase + 10] << 16) | (ioRegisters[paramBase + 11] << 24);
    uint32_t yRaw = ioRegisters[paramBase + 12] | (ioRegisters[paramBase + 13] << 8)
        | (ioRegisters[paramBase + 14] << 16) | (ioRegisters[paramBase + 15] << 24);

    //reference point is a signed 28-bit value stored in a 32-bit field
    int32_t x0 = static_cast<int32_t>(xRaw << 4) >> 4;
    int32_t y0 = static_cast<int32_t>(yRaw << 4) >> 4;

    int32_t textureX = (x0 + pa * screenX + pb * screenY) >> 8;
    int32_t textureY = (y0 + pc * screenX + pd * screenY) >> 8;

    uint8_t sizeMode = (bgcnt >> 14) & 0x3;
    int mapSizeTiles = 16 << sizeMode;
    int mapSizePixels = mapSizeTiles * 8;

    bool wrap = (bgcnt & 0x2000) != 0;
    if (wrap)
    {
        textureX = ((textureX % mapSizePixels) + mapSizePixels) % mapSizePixels;
        textureY = ((textureY % mapSizePixels) + mapSizePixels) % mapSizePixels;
    }
    else if (textureX < 0 || textureX >= mapSizePixels || textureY < 0 || textureY >= mapSizePixels)
    {
        return false;
    }

    int tileX = textureX / 8;
    int tileY = textureY / 8;
    int inTileX = textureX % 8;
    int inTileY = textureY % 8;

    uint8_t screenBaseBlock = (bgcnt >> 8) & 0x1F;
    uint32_t mapBase = screenBaseBlock * 0x800u;
    uint32_t entryAddr = mapBase + static_cast<uint32_t>(tileY * mapSizeTiles + tileX);
    if (entryAddr >= vram.size())
        return false;

    //affine tilemap entries are an 8-bit tile index
    uint8_t tileNumber = vram[entryAddr];

    uint8_t charBaseBlock = (bgcnt >> 2) & 0x3;
    uint32_t charBase = charBaseBlock * 0x4000u;
    uint32_t tileAddr = charBase + tileNumber * 64u + inTileY * 8u + inTileX;
    if (tileAddr >= vram.size())
        return false;

    uint8_t colorIndex = vram[tileAddr];
    if (colorIndex == 0)
        return false;

    outColor = PaletteColor(false, colorIndex);
    return true;
}

bool PPU::SampleBitmapMode3(int screenX, int screenY, uint32_t& outColor)
{
    uint32_t addr = static_cast<uint32_t>(screenY * 240 + screenX) * 2u;
    uint16_t color = static_cast<uint16_t>(vram[addr] | (vram[addr + 1] << 8));
    outColor = Bgr555ToArgb(color);
    return true;
}

bool PPU::SampleBitmapMode4(int screenX, int screenY, uint16_t dispcnt, uint32_t& outColor)
{
    uint32_t base = (dispcnt & 0x10) ? 0xA000u : 0u;
    uint8_t index = vram[base + screenY * 240 + screenX];
    if (index == 0)
        return false;
    outColor = PaletteColor(false, index);
    return true;
}

bool PPU::SampleBitmapMode5(int screenX, int screenY, uint16_t dispcnt, uint32_t& outColor)
{
    //mode 5 only covers 160x128, the rest of the screen stays backdrop
    if (screenX >= 160 || screenY >= 128)
        return false;

    uint32_t base = (dispcnt & 0x10) ? 0xA000u : 0u;
    uint32_t addr = base + static_cast<uint32_t>(screenY * 160 + screenX) * 2u;
    uint16_t color = static_cast<uint16_t>(vram[addr] | (vram[addr + 1] << 8));
    outColor = Bgr555ToArgb(color);
    return true;
}

void PPU::BuildSpriteLine(int screenY)
{
    for (auto& px : spriteLine)
        px.opaque = false;
    objWindowLine.fill(false);

    bool oneDMapping = (ioRegisters[0x000] & 0x40) != 0;

    static constexpr int widths[4][4]  = { {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}, {0, 0, 0, 0} };
    static constexpr int heights[4][4] = { {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}, {0, 0, 0, 0} };

    for (int obj = 127; obj >= 0; obj--)
    {
        uint32_t base = static_cast<uint32_t>(obj) * 8;
        uint16_t attr0 = static_cast<uint16_t>(oam[base] | (oam[base + 1] << 8));
        uint16_t attr1 = static_cast<uint16_t>(oam[base + 2] | (oam[base + 3] << 8));
        uint16_t attr2 = static_cast<uint16_t>(oam[base + 4] | (oam[base + 5] << 8));

        bool affine = (attr0 & 0x100) != 0;
        bool disableOrDoubleSize = (attr0 & 0x200) != 0;
        if (!affine && disableOrDoubleSize)
            continue;

        uint8_t objMode = (attr0 >> 10) & 0x3;
        bool isWindowSprite = (objMode == 2);

        uint8_t shape = (attr0 >> 14) & 0x3;
        uint8_t size = (attr1 >> 14) & 0x3;
        if (shape == 3)
            continue;

        int spriteWidth = widths[shape][size];
        int spriteHeight = heights[shape][size];

        bool doubleSize = affine && disableOrDoubleSize;
        int boundingWidth = doubleSize ? spriteWidth * 2 : spriteWidth;
        int boundingHeight = doubleSize ? spriteHeight * 2 : spriteHeight;

        int yCoord = attr0 & 0xFF;
        int screenY0 = (yCoord >= 160) ? yCoord - 256 : yCoord;
        int xCoordRaw = attr1 & 0x1FF;
        int screenX0 = (xCoordRaw >= 256) ? xCoordRaw - 512 : xCoordRaw;

        if (screenX0 + boundingWidth <= 0 || screenX0 >= 240) continue;
        if (screenY0 + boundingHeight <= 0 || screenY0 >= 160) continue;

        bool is8bpp = (attr0 & 0x2000) != 0;
        uint16_t tileNumber = attr2 & 0x3FF;
        uint8_t priority = (attr2 >> 10) & 0x3;
        uint8_t paletteBank = (attr2 >> 12) & 0xF;

        int16_t pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine)
        {
            uint8_t paramSel = (attr1 >> 9) & 0x1F;
            uint32_t groupBase = static_cast<uint32_t>(paramSel) * 32;
            pa = static_cast<int16_t>(oam[groupBase + 6] | (oam[groupBase + 7] << 8));
            pb = static_cast<int16_t>(oam[groupBase + 14] | (oam[groupBase + 15] << 8));
            pc = static_cast<int16_t>(oam[groupBase + 22] | (oam[groupBase + 23] << 8));
            pd = static_cast<int16_t>(oam[groupBase + 30] | (oam[groupBase + 31] << 8));
        }

        bool flipX = false, flipY = false;
        if (!affine)
        {
            flipX = (attr1 & 0x1000) != 0;
            flipY = (attr1 & 0x2000) != 0;
        }

        static constexpr uint32_t charBase = 0x10000;
        int centerX = boundingWidth / 2;
        int centerY = boundingHeight / 2;

        int by = screenY - screenY0;
        if (by >= 0 && by < boundingHeight)
        {
            for (int bx = 0; bx < boundingWidth; bx++)
            {
                int screenX = screenX0 + bx;
                if (screenX < 0 || screenX >= 240)
                    continue;

                int texX, texY;
                if (affine)
                {
                    int relX = bx - centerX;
                    int relY = by - centerY;
                    int32_t tx = (pa * relX + pb * relY) >> 8;
                    int32_t ty = (pc * relX + pd * relY) >> 8;
                    texX = tx + spriteWidth / 2;
                    texY = ty + spriteHeight / 2;
                    if (texX < 0 || texX >= spriteWidth || texY < 0 || texY >= spriteHeight)
                        continue;
                }
                else
                {
                    texX = flipX ? (spriteWidth - 1 - bx) : bx;
                    texY = flipY ? (spriteHeight - 1 - by) : by;
                }

                int tileX = texX / 8;
                int tileY = texY / 8;
                int inX = texX % 8;
                int inY = texY % 8;

                uint32_t tileIndex = oneDMapping
                    ? tileNumber + (tileY * (spriteWidth / 8) + tileX) * (is8bpp ? 2u : 1u)
                    : tileNumber + tileY * 32u + tileX * (is8bpp ? 2u : 1u);
                uint32_t tileAddr = charBase + tileIndex * 32u;

                uint8_t colorIndex;
                if (is8bpp)
                {
                    uint32_t addr = tileAddr + inY * 8u + inX;
                    if (addr >= vram.size()) continue;
                    colorIndex = vram[addr];
                }
                else
                {
                    uint32_t addr = tileAddr + inY * 4u + inX / 2u;
                    if (addr >= vram.size()) continue;
                    uint8_t byteVal = vram[addr];
                    colorIndex = (inX & 1) ? static_cast<uint8_t>(byteVal >> 4) : static_cast<uint8_t>(byteVal & 0xF);
                }

                if (colorIndex == 0)
                    continue;

                if (isWindowSprite)
                {
                    objWindowLine[screenX] = true;
                    continue;
                }

                SpritePixel& destination = spriteLine[screenX];
                
                if (destination.opaque && priority > destination.priority)
                    continue;

                int paletteIndex = is8bpp ? colorIndex : (paletteBank * 16 + colorIndex);
                destination.color = PaletteColor(true, paletteIndex);
                destination.priority = priority;
                destination.opaque = true;
                //if da mode 1 then it trans(gender)
                destination.semiTransparent = (objMode == 1);
            }
        }
    }
}

bool PPU::PointInWindow(int index, int x, int y)
{
    uint32_t hOffset = (index == 0) ? 0x040 : 0x042;
    uint32_t vOffset = (index == 0) ? 0x044 : 0x046;

    //high byte = left/top (X1/Y1), low byte = right/bottom (X2/Y2, exclusive)
    uint8_t x1 = ioRegisters[hOffset + 1];
    uint8_t x2 = ioRegisters[hOffset];
    uint8_t y1 = ioRegisters[vOffset + 1];
    uint8_t y2 = ioRegisters[vOffset];

    int right = x2;
    if (right > 240 || right < x1) right = 240;
    int bottom = y2;
    if (bottom > 160 || bottom < y1) bottom = 160;

    return x >= x1 && x < right && y >= y1 && y < bottom;
}

uint8_t PPU::GetWindowLayerMask(int x, int y)
{
    uint16_t dispcnt = static_cast<uint16_t>(ioRegisters[0x000] | (ioRegisters[0x001] << 8));
    bool win0Enabled = (dispcnt & 0x2000) != 0;
    bool win1Enabled = (dispcnt & 0x4000) != 0;
    bool objWinEnabled = (dispcnt & 0x8000) != 0;

    if (!win0Enabled && !win1Enabled && !objWinEnabled)
        return 0x3F;

    if (win0Enabled && PointInWindow(0, x, y))
        return ioRegisters[0x048]; //WININ low byte - Window 0 control
    if (win1Enabled && PointInWindow(1, x, y))
        return ioRegisters[0x049]; //WININ high byte - Window 1 control
    if (objWinEnabled && objWindowLine[x])
        return ioRegisters[0x04B]; //WINOUT high byte - OBJ window control
    return ioRegisters[0x04A]; //WINOUT low byte - outside control
}

void PPU::RenderFrame(uint32_t* pixels)
{
    std::copy(latchedFrame.begin(), latchedFrame.end(), pixels);
}

void PPU::ComposeScanline(int screenY)
{
    uint32_t* pixels = latchedFrame.data() + screenY * 240;

    uint16_t dispcnt = static_cast<uint16_t>(ioRegisters[0x000] | (ioRegisters[0x001] << 8));
    uint8_t mode = dispcnt & 0x7;

    //force white screen
    if (dispcnt & 0x80)
    {
        for (int x = 0; x < 240; x++)
            pixels[x] = 0xFFFFFFFFu;
        return;
    }

    uint32_t backdrop = PaletteColor(false, 0);
    ResetPixelLine(backdrop);

    bool objEnabled = (dispcnt & 0x1000) != 0;
    bool objWinEnabled = (dispcnt & 0x8000) != 0;
    if (objEnabled || objWinEnabled)
        BuildSpriteLine(screenY);

    if (mode == 3 || mode == 4 || mode == 5)
    {
        for (int x = 0; x < 240; x++)
        {
            uint32_t color;
            bool opaque = false;
            if (mode == 3) opaque = SampleBitmapMode3(x, screenY, color);
            else if (mode == 4) opaque = SampleBitmapMode4(x, screenY, dispcnt, color);
            else opaque = SampleBitmapMode5(x, screenY, dispcnt, color);

            //BIT MAP!
            if (opaque)
                PushPixel(x, color, LAYER_BG2);
        }

        if (objEnabled)
        {
            for (int x = 0; x < 240; x++)
                if (spriteLine[x].opaque)
                    PushPixel(x, spriteLine[x].color, LAYER_OBJ, spriteLine[x].semiTransparent);
        }

        ApplyColorEffects(screenY);
        return;
    }

    bool bgEnabled[4] = {
        (dispcnt & 0x100) != 0, (dispcnt & 0x200) != 0, (dispcnt & 0x400) != 0, (dispcnt & 0x800) != 0
    };
    bool isAffine[4] = { false, false, false, false };

    if (mode == 1) { isAffine[2] = true; bgEnabled[3] = false; }
    if (mode == 2) { bgEnabled[0] = false; bgEnabled[1] = false; isAffine[2] = true; isAffine[3] = true; }

    int priority[4] = { -1, -1, -1, -1 };
    for (int i = 0; i < 4; i++)
    {
        if (!bgEnabled[i]) continue;
        uint32_t bgCntOffset = 0x08 + i * 2;
        uint16_t bgcnt = static_cast<uint16_t>(ioRegisters[bgCntOffset] | (ioRegisters[bgCntOffset + 1] << 8));
        priority[i] = bgcnt & 0x3;
    }

    //#the painter
    for (int p = 3; p >= 0; p--)
    {
        for (int bg = 3; bg >= 0; bg--)
        {
            if (!bgEnabled[bg] || priority[bg] != p)
                continue;

            for (int x = 0; x < 240; x++)
            {
                if (!(GetWindowLayerMask(x, screenY) & (1 << bg)))
                    continue;

                uint32_t color;
                bool opaque = isAffine[bg]
                    ? SampleAffineBackground(bg, x, screenY, color)
                    : SampleRegularBackground(bg, x, screenY, color);
                if (opaque)
                    PushPixel(x, color, static_cast<uint8_t>(LAYER_BG0 + bg));
            }
        }

        if (objEnabled)
        {
            for (int x = 0; x < 240; x++)
            {
                if (spriteLine[x].opaque && spriteLine[x].priority == p
                    && (GetWindowLayerMask(x, screenY) & 0x10))
                    PushPixel(x, spriteLine[x].color, LAYER_OBJ, spriteLine[x].semiTransparent);
            }
        }
    }

    ApplyColorEffects(screenY);
}

void PPU::ResetPixelLine(uint32_t backdrop)
{
    for (auto& stack : pixelLine)
    {
        stack.topColor = backdrop;
        stack.secondColor = backdrop;
        stack.topLayer = LAYER_BACKDROP;
        stack.secondLayer = LAYER_BACKDROP;
        stack.topSemiTransparent = false;
    }
}

void PPU::PushPixel(int x, uint32_t color, uint8_t layer, bool semiTransparent)
{
    //compositing runs bottom up, so whatever was on top is now the layer underneath
    PixelStack& stack = pixelLine[x];
    stack.secondColor = stack.topColor;
    stack.secondLayer = stack.topLayer;
    stack.topColor = color;
    stack.topLayer = layer;
    stack.topSemiTransparent = semiTransparent;
}

void PPU::ApplyColorEffects(int screenY)
{
    uint32_t* pixels = latchedFrame.data() + screenY * 240;

    const uint16_t bldcnt = static_cast<uint16_t>(ioRegisters[0x050] | (ioRegisters[0x051] << 8));
    const uint16_t bldalpha = static_cast<uint16_t>(ioRegisters[0x052] | (ioRegisters[0x053] << 8));

    const uint8_t effect = (bldcnt >> 6) & 0x3;
    const uint16_t firstTarget = bldcnt & 0x3F;
    const uint16_t secondTarget = (bldcnt >> 8) & 0x3F;

    const uint32_t eva = std::min<uint32_t>(bldalpha & 0x1F, 16);
    const uint32_t evb = std::min<uint32_t>((bldalpha >> 8) & 0x1F, 16);
    const uint32_t evy = std::min<uint32_t>(ioRegisters[0x054] & 0x1F, 16);

    auto channels = [](uint32_t argb, uint32_t& r, uint32_t& g, uint32_t& b)
    {
        r = (argb >> 19) & 0x1F;
        g = (argb >> 11) & 0x1F;
        b = (argb >> 3) & 0x1F;
    };

    auto pack = [](uint32_t r, uint32_t g, uint32_t b)
    {
        const uint32_t r8 = (r << 3) | (r >> 2);
        const uint32_t g8 = (g << 3) | (g >> 2);
        const uint32_t b8 = (b << 3) | (b >> 2);
        return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
    };

    for (int x = 0; x < 240; x++)
    {
        {
            const PixelStack& stack = pixelLine[x];

            const bool effectAllowedHere = (GetWindowLayerMask(x, screenY) & 0x20) != 0;

            const bool topIsFirstTarget = (firstTarget & (1u << stack.topLayer)) != 0;
            const bool secondIsSecondTarget = (secondTarget & (1u << stack.secondLayer)) != 0;

            //semi transgender
            const bool semiBlend = stack.topSemiTransparent && secondIsSecondTarget;

            uint32_t result = stack.topColor;

            if (effectAllowedHere && (semiBlend || (effect == 1 && topIsFirstTarget && secondIsSecondTarget)))
            {
                uint32_t r1, g1, b1, r2, g2, b2;
                channels(stack.topColor, r1, g1, b1);
                channels(stack.secondColor, r2, g2, b2);

                result = pack(
                    std::min<uint32_t>(31, (r1 * eva + r2 * evb) / 16),
                    std::min<uint32_t>(31, (g1 * eva + g2 * evb) / 16),
                    std::min<uint32_t>(31, (b1 * eva + b2 * evb) / 16));
            }
            else if (effectAllowedHere && topIsFirstTarget && (effect == 2 || effect == 3))
            {
                uint32_t r, g, b;
                channels(stack.topColor, r, g, b);

                if (effect == 2)
                {
                    //brightness increase, fade towards white
                    r += ((31 - r) * evy) / 16;
                    g += ((31 - g) * evy) / 16;
                    b += ((31 - b) * evy) / 16;
                }
                else
                {
                    //brightness decrease, fade towards black
                    r -= (r * evy) / 16;
                    g -= (g * evy) / 16;
                    b -= (b * evy) / 16;
                }

                result = pack(r, g, b);
            }

            pixels[x] = result;
        }
    }
}
