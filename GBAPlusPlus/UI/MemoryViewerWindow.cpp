#include "MemoryViewerWindow.h"
#include "../AGB/MemoryBus.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

MemoryViewerWindow::MemoryViewerWindow(const char* title, int x, int y, int width, int height)
    : window(nullptr)
    , renderer(nullptr)
    , fontMono(nullptr)
    , fontUI(nullptr)
    , isOpen(true)
    , memoryBus(nullptr)
    , viewOffset(0)
    , bytesPerRow(16)
    , visibleRows(0)
    , scrollY(0)
    , isDraggingScrollbar(false)
    , selectedAddress(0)
    , hasSelection(false)
    , addressColumnWidth(120)
    , hexColumnWidth(600)
    , asciiColumnWidth(200)
    , rowHeight(20)
    , headerHeight(30)
    , padding(10)
    , showGotoDialog(false)
    , gotoInputActive(false)
{
    window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        isOpen = false;
        return;
    }
    
    SDL_SetWindowPosition(window, x, y);
    
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
        isOpen = false;
        return;
    }
    
    fontMono = TTF_OpenFont("Assets/cour.ttf", 14);
    fontUI = TTF_OpenFont("Assets/cour.ttf", 12);
    
    if (!fontMono || !fontUI) {
        std::cerr << "Failed to load fonts: " << SDL_GetError() << std::endl;
    }
    
    initializeRegions();
    calculateLayout();
}

MemoryViewerWindow::~MemoryViewerWindow() {
    clearCache();
    
    if (renderer) SDL_DestroyRenderer(renderer);
    if (fontMono) TTF_CloseFont(fontMono);
    if (fontUI) TTF_CloseFont(fontUI);
    if (window) SDL_DestroyWindow(window);
}

void MemoryViewerWindow::initializeRegions() {
    regions = {
        {"BIOS",        0x00000000, 0x00004000, {100, 149, 237, 255}},  // Cornflower blue
        {"EWRAM",       0x02000000, 0x00040000, {144, 238, 144, 255}},  // Light green
        {"IWRAM",       0x03000000, 0x00008000, {255, 218, 185, 255}},  // Peach
        {"I/O",         0x04000000, 0x00000400, {255, 182, 193, 255}},  // Light pink
        {"Palette",     0x05000000, 0x00000400, {221, 160, 221, 255}},  // Plum
        {"VRAM",        0x06000000, 0x00018000, {173, 216, 230, 255}},  // Light blue
        {"OAM",         0x07000000, 0x00000400, {255, 222, 173, 255}},  // Navajo white
        {"ROM WS0",     0x08000000, 0x02000000, {255, 250, 205, 255}},  // Lemon chiffon
        {"ROM WS1",     0x0A000000, 0x02000000, {255, 245, 157, 255}},  // Light yellow
        {"ROM WS2",     0x0C000000, 0x02000000, {255, 239, 127, 255}},  // Pale yellow
        {"SRAM",        0x0E000000, 0x00010000, {216, 191, 216, 255}},  // Thistle
    };
}

void MemoryViewerWindow::calculateLayout() {
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    
    visibleRows = (height - headerHeight - padding * 2) / rowHeight;
    
    // Scrollbar
    scrollbarRect = {
        (float)(width - 20),
        (float)headerHeight,
        15.0f,
        (float)(height - headerHeight)
    };
    
    // Goto dialog (centered)
    float dialogWidth = 400;
    float dialogHeight = 150;
    gotoDialogRect = {
        (width - dialogWidth) / 2.0f,
        (height - dialogHeight) / 2.0f,
        dialogWidth,
        dialogHeight
    };
    
    gotoInputRect = {
        gotoDialogRect.x + 20,
        gotoDialogRect.y + 50,
        gotoDialogRect.w - 40,
        35
    };
    
    gotoButtonRect = {
        gotoDialogRect.x + gotoDialogRect.w - 170,
        gotoDialogRect.y + gotoDialogRect.h - 50,
        70,
        35
    };
    
    gotoCancelButtonRect = {
        gotoDialogRect.x + gotoDialogRect.w - 90,
        gotoDialogRect.y + gotoDialogRect.h - 50,
        70,
        35
    };
    
    updateScrollbarThumb();
}

void MemoryViewerWindow::setMemoryBus(MemoryBus* bus) {
    memoryBus = bus;
    clearCache();
}

void MemoryViewerWindow::render() {
    if (!isOpen) return;
    
    // Clear background
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    
    renderHeader();
    renderMemoryRows();
    renderScrollbar();
    
    // Render goto dialog on top if active
    if (showGotoDialog) {
        renderGotoDialog();
    }
    
    SDL_RenderPresent(renderer);
}

void MemoryViewerWindow::renderHeader() {
    // Header background
    SDL_FRect headerRect = {0, 0, 10000, (float)headerHeight};
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderFillRect(renderer, &headerRect);
    
    // Column headers
    std::stringstream header;
    header << "Address        ";
    for (int i = 0; i < bytesPerRow; i++) {
        header << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << i << " ";
    }
    header << "  ASCII";
    
    SDL_Texture* headerTexture = createTextTexture(header.str(), fontMono, {200, 200, 200, 255});
    if (headerTexture) {
        float w, h;
        SDL_GetTextureSize(headerTexture, &w, &h);
        SDL_FRect rect = {(float)padding, (float)(headerHeight - h) / 2, w, h};
        SDL_RenderTexture(renderer, headerTexture, nullptr, &rect);
        SDL_DestroyTexture(headerTexture);
    }
}

void MemoryViewerWindow::renderMemoryRows() {
    if (!memoryBus) return;
    
    int yPos = headerHeight + padding;
    uint32_t currentAddress = viewOffset;
    
    for (int row = 0; row < visibleRows && currentAddress < 0x10000000; row++) {
        // Address column
        std::stringstream addrStr;
        addrStr << "0x" << std::hex << std::uppercase 
                << std::setw(8) << std::setfill('0') << currentAddress;
        
        SDL_Color textColor = {200, 200, 200, 255};
        SDL_Texture* addrTexture = createTextTexture(addrStr.str(), fontMono, textColor);
        
        if (addrTexture) {
            float w, h;
            SDL_GetTextureSize(addrTexture, &w, &h);
            SDL_FRect rect = {(float)padding, (float)yPos, w, h};
            SDL_RenderTexture(renderer, addrTexture, nullptr, &rect);
            SDL_DestroyTexture(addrTexture);
        }
        
        // Hex bytes
        std::stringstream hexStr;
        std::stringstream asciiStr;
        
        int xOffset = padding + addressColumnWidth;
        
        for (int col = 0; col < bytesPerRow; col++) {
            uint32_t addr = currentAddress + col;
            uint8_t byte = memoryBus->read8(addr);
            
            // Get color for this memory region
            SDL_Color byteColor = getColorForAddress(addr);
            
            // Highlight selection
            if (hasSelection && addr == selectedAddress) {
                SDL_FRect highlightRect = {
                    (float)(xOffset + col * 30),
                    (float)yPos,
                    25.0f,
                    (float)rowHeight
                };
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 100);
                SDL_RenderFillRect(renderer, &highlightRect);
            }
            
            // Render hex byte
            std::stringstream singleByte;
            singleByte << std::hex << std::uppercase 
                      << std::setw(2) << std::setfill('0') << (int)byte;
            
            SDL_Texture* byteTexture = createTextTexture(singleByte.str(), fontMono, byteColor);
            if (byteTexture) {
                float w, h;
                SDL_GetTextureSize(byteTexture, &w, &h);
                SDL_FRect rect = {
                    (float)(xOffset + col * 30),
                    (float)yPos,
                    w, h
                };
                SDL_RenderTexture(renderer, byteTexture, nullptr, &rect);
                SDL_DestroyTexture(byteTexture);
            }
            
            // Build ASCII representation
            if (byte >= 32 && byte < 127) {
                asciiStr << (char)byte;
            } else {
                asciiStr << '.';
            }
        }
        
        // Render ASCII column
        int asciiXOffset = xOffset + bytesPerRow * 30 + 20;
        SDL_Texture* asciiTexture = createTextTexture(asciiStr.str(), fontMono, {150, 150, 150, 255});
        if (asciiTexture) {
            float w, h;
            SDL_GetTextureSize(asciiTexture, &w, &h);
            SDL_FRect rect = {(float)asciiXOffset, (float)yPos, w, h};
            SDL_RenderTexture(renderer, asciiTexture, nullptr, &rect);
            SDL_DestroyTexture(asciiTexture);
        }
        
        // Draw region label on the right
        std::string regionName = getRegionNameForAddress(currentAddress);
        if (!regionName.empty() && (currentAddress & 0xFFFFFFF0) == currentAddress) {
            SDL_Texture* labelTexture = createTextTexture(regionName, fontUI, {128, 128, 128, 255});
            if (labelTexture) {
                float w, h;
                SDL_GetTextureSize(labelTexture, &w, &h);
                int windowWidth;
                SDL_GetWindowSize(window, &windowWidth, nullptr);
                SDL_FRect rect = {
                    (float)(windowWidth - w - 40),
                    (float)yPos,
                    w, h
                };
                SDL_RenderTexture(renderer, labelTexture, nullptr, &rect);
                SDL_DestroyTexture(labelTexture);
            }
        }
        
        yPos += rowHeight;
        currentAddress += bytesPerRow;
    }
}

void MemoryViewerWindow::renderScrollbar() {
    // Scrollbar background
    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(renderer, &scrollbarRect);
    
    // Scrollbar thumb
    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    SDL_RenderFillRect(renderer, &scrollbarThumbRect);
}

void MemoryViewerWindow::updateScrollbarThumb() {
    uint32_t totalMemory = 0x10000000; // 256MB addressable space
    uint32_t viewableMemory = visibleRows * bytesPerRow;
    
    float thumbHeight = scrollbarRect.h * ((float)viewableMemory / totalMemory);
    thumbHeight = std::max(20.0f, thumbHeight); // Minimum thumb size
    
    float scrollPercent = (float)viewOffset / (totalMemory - viewableMemory);
    float thumbY = scrollbarRect.y + scrollPercent * (scrollbarRect.h - thumbHeight);
    
    scrollbarThumbRect = {
        scrollbarRect.x,
        thumbY,
        scrollbarRect.w,
        thumbHeight
    };
}

SDL_Color MemoryViewerWindow::getColorForAddress(uint32_t address) {
    // Check which region based on top byte (just like MemoryBus does)
    uint8_t region = address >> 24;
    
    switch (region) {
        case 0x00:
            // BIOS is only 16KB (0x00000000 - 0x00003FFF)
            if (address < 0x00004000)
                return {100, 149, 237, 255};  // BIOS - Cornflower blue
            else
                return {80, 80, 80, 255};     // Unmapped - Dark gray
            
        case 0x02: return {144, 238, 144, 255};  // EWRAM - Light green
        case 0x03: return {255, 218, 185, 255};  // IWRAM - Peach
        case 0x04:
            // I/O is only 1KB (0x04000000 - 0x040003FF)
            if ((address & 0xFFFFFF) < 0x000400)
                return {255, 182, 193, 255};  // I/O - Light pink
            else
                return {80, 80, 80, 255};     // Unmapped - Dark gray
                
        case 0x05: return {221, 160, 221, 255};  // Palette - Plum
        case 0x06: return {173, 216, 230, 255};  // VRAM - Light blue
        case 0x07: return {255, 222, 173, 255};  // OAM - Navajo white
        case 0x08: case 0x09:                    // ROM WS0
            return {255, 250, 205, 255};         // Lemon chiffon
        case 0x0A: case 0x0B:                    // ROM WS1
            return {255, 245, 157, 255};         // Light yellow
        case 0x0C: case 0x0D:                    // ROM WS2
            return {255, 239, 127, 255};         // Pale yellow
        case 0x0E: case 0x0F:                    // SRAM
            return {216, 191, 216, 255};         // Thistle
        default:
            return {80, 80, 80, 255};            // Unmapped - Dark gray
    }
}

std::string MemoryViewerWindow::getRegionNameForAddress(uint32_t address) {
    uint8_t region = address >> 24;
    uint32_t offsetInRegion = address & 0xFFFFFF;
    
    // Only show label at the start of each region
    if (offsetInRegion != 0) return "";
    
    switch (region) {
        case 0x00: return "BIOS";
        case 0x02: return "EWRAM";
        case 0x03: return "IWRAM";
        case 0x04: return "I/O Registers";
        case 0x05: return "Palette RAM";
        case 0x06: return "VRAM";
        case 0x07: return "OAM";
        case 0x08: return "ROM WS0";
        case 0x09: return "ROM WS0 (Mirror)";
        case 0x0A: return "ROM WS1";
        case 0x0B: return "ROM WS1 (Mirror)";
        case 0x0C: return "ROM WS2";
        case 0x0D: return "ROM WS2 (Mirror)";
        case 0x0E: return "SRAM";
        case 0x0F: return "SRAM (Mirror)";
        default: return "";
    }
}

SDL_Texture* MemoryViewerWindow::createTextTexture(const std::string& text, TTF_Font* font, SDL_Color color) {
    if (!font || text.empty()) return nullptr;
    
    SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), 0, color);
    if (!surface) return nullptr;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    
    return texture;
}

void MemoryViewerWindow::handleEvents(SDL_Event& event) {
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (event.window.windowID == SDL_GetWindowID(window)) {
            isOpen = false;
        }
    }
    else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        if (event.window.windowID == SDL_GetWindowID(window)) {
            calculateLayout();
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        handleKeyPress(event.key);
    }
    else if (event.type == SDL_EVENT_TEXT_INPUT) {
        handleTextInput(event.text);
    }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (!showGotoDialog) {
            handleMouseWheel(event.wheel);
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        handleMouseButton(event.button);
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            isDraggingScrollbar = false;
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (!showGotoDialog) {
            handleMouseMotion(event.motion);
        }
    }
}

void MemoryViewerWindow::handleMouseWheel(const SDL_MouseWheelEvent& wheel) {
    int scrollAmount = wheel.y * 3 * bytesPerRow; // Scroll 3 rows at a time
    
    int64_t newOffset = (int64_t)viewOffset - scrollAmount;
    newOffset = std::max(0LL, std::min(newOffset, (int64_t)(0x10000000 - visibleRows * bytesPerRow)));
    
    viewOffset = (uint32_t)newOffset;
    viewOffset &= ~0xF; // Align to 16 bytes
    
    updateScrollbarThumb();
}

void MemoryViewerWindow::handleMouseButton(const SDL_MouseButtonEvent& button) {
    if (button.button == SDL_BUTTON_LEFT) {
        float x = button.x;
        float y = button.y;
        
        // Check if clicking goto dialog buttons
        if (showGotoDialog) {
            if (isPointInRect(x, y, gotoButtonRect)) {
                executeGoto();
                return;
            }
            if (isPointInRect(x, y, gotoCancelButtonRect)) {
                hideGoto();
                return;
            }
            if (isPointInRect(x, y, gotoInputRect)) {
                gotoInputActive = true;
                SDL_StartTextInput(window);
                return;
            }
            // Click outside dialog closes it
            if (!isPointInRect(x, y, gotoDialogRect)) {
                hideGoto();
                return;
            }
            return;
        }
        
        // Check if clicking scrollbar
        if (isPointInRect(x, y, scrollbarRect)) {
            isDraggingScrollbar = true;
        }
        
        // Check if clicking memory address
        uint32_t clickedAddress = getAddressAtPosition((int)x, (int)y);
        if (clickedAddress != 0xFFFFFFFF) {
            selectedAddress = clickedAddress;
            hasSelection = true;
        }
    }
}

void MemoryViewerWindow::handleMouseMotion(const SDL_MouseMotionEvent& motion) {
    if (isDraggingScrollbar) {
        float scrollPercent = (motion.y - scrollbarRect.y) / scrollbarRect.h;
        scrollPercent = std::max(0.0f, std::min(1.0f, scrollPercent));
        
        uint32_t totalMemory = 0x10000000;
        uint32_t viewableMemory = visibleRows * bytesPerRow;
        
        viewOffset = (uint32_t)(scrollPercent * (totalMemory - viewableMemory));
        viewOffset &= ~0xF; // Align to 16 bytes
        
        updateScrollbarThumb();
    }
}

uint32_t MemoryViewerWindow::getAddressAtPosition(int x, int y) {
    if (y < headerHeight || y > headerHeight + visibleRows * rowHeight) {
        return 0xFFFFFFFF;
    }
    
    int row = (y - headerHeight) / rowHeight;
    int hexStart = padding + addressColumnWidth;
    
    if (x < hexStart || x > hexStart + bytesPerRow * 30) {
        return 0xFFFFFFFF;
    }
    
    int col = (x - hexStart) / 30;
    
    return viewOffset + row * bytesPerRow + col;
}

bool MemoryViewerWindow::isPointInRect(float x, float y, const SDL_FRect& rect) {
    return x >= rect.x && x <= rect.x + rect.w &&
           y >= rect.y && y <= rect.y + rect.h;
}

void MemoryViewerWindow::update() {
    // Called each frame for any continuous updates
}

void MemoryViewerWindow::scrollTo(uint32_t address) {
    viewOffset = address & ~0xF; // Align to 16 bytes
    updateScrollbarThumb();
}

void MemoryViewerWindow::setSelection(uint32_t address) {
    selectedAddress = address;
    hasSelection = true;
    
    // Scroll to make selection visible if needed
    if (address < viewOffset || address >= viewOffset + visibleRows * bytesPerRow) {
        scrollTo(address);
    }
}

void MemoryViewerWindow::clearCache() {
    for (auto& row : cachedRows) {
        if (row.addressTexture) SDL_DestroyTexture(row.addressTexture);
        if (row.hexTexture) SDL_DestroyTexture(row.hexTexture);
        if (row.asciiTexture) SDL_DestroyTexture(row.asciiTexture);
    }
    cachedRows.clear();
}

SDL_WindowID MemoryViewerWindow::getWindowId() const {
    return SDL_GetWindowID(window);
}

void MemoryViewerWindow::handleKeyPress(const SDL_KeyboardEvent& key) {
    if (showGotoDialog) {
        if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) {
            executeGoto();
        }
        else if (key.key == SDLK_ESCAPE) {
            hideGoto();
        }
        else if (key.key == SDLK_BACKSPACE && !gotoInputText.empty()) {
            gotoInputText.pop_back();
        }
    }
    else {
        // Ctrl+G or G to open goto dialog
        if ((key.key == SDLK_G && (key.mod & SDL_KMOD_CTRL)) || key.key == SDLK_G) {
            showGoto();
            SDL_StartTextInput(window);
        }
    }
}

void MemoryViewerWindow::handleTextInput(const SDL_TextInputEvent& text) {
    if (showGotoDialog && gotoInputActive) {
        // Only accept hex characters and 0x prefix
        std::string input = text.text;
        for (char c : input) {
            if ((c >= '0' && c <= '9') || 
                (c >= 'a' && c <= 'f') || 
                (c >= 'A' && c <= 'F') ||
                c == 'x' || c == 'X') {
                gotoInputText += c;
            }
        }
    }
}

void MemoryViewerWindow::executeGoto() {
    uint32_t address = parseHexAddress(gotoInputText);
    scrollTo(address);
    setSelection(address);
    hideGoto();
    SDL_StopTextInput(window);
}

uint32_t MemoryViewerWindow::parseHexAddress(const std::string& text) {
    if (text.empty()) return 0;
    
    std::string hexStr = text;
    
    // Remove "0x" or "0X" prefix if present
    if (hexStr.length() >= 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
        hexStr = hexStr.substr(2);
    }
    
    // Parse hex string
    uint32_t address = 0;
    try {
        address = std::stoul(hexStr, nullptr, 16);
    }
    catch (...) {
        address = 0;
    }
    
    return address;
}

void MemoryViewerWindow::renderGotoDialog() {
    // Semi-transparent overlay
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    SDL_FRect overlay = {0, 0, (float)width, (float)height};
    SDL_RenderFillRect(renderer, &overlay);
    
    // Dialog background
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderFillRect(renderer, &gotoDialogRect);
    
    // Dialog border
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderRect(renderer, &gotoDialogRect);
    
    // Title
    SDL_Texture* titleTexture = createTextTexture("Go to Address", fontUI, {255, 255, 255, 255});
    if (titleTexture) {
        float w, h;
        SDL_GetTextureSize(titleTexture, &w, &h);
        SDL_FRect titleRect = {
            gotoDialogRect.x + 20,
            gotoDialogRect.y + 15,
            w, h
        };
        SDL_RenderTexture(renderer, titleTexture, nullptr, &titleRect);
        SDL_DestroyTexture(titleTexture);
    }
    
    // Input box background
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderFillRect(renderer, &gotoInputRect);
    
    // Input box border
    SDL_SetRenderDrawColor(renderer, gotoInputActive ? 100 : 70, gotoInputActive ? 150 : 70, gotoInputActive ? 255 : 70, 255);
    SDL_RenderRect(renderer, &gotoInputRect);
    
    // Input text
    std::string displayText = gotoInputText;
    if (gotoInputActive && (SDL_GetTicks() / 500) % 2 == 0) {
        displayText += "_"; // Blinking cursor
    }
    
    if (!displayText.empty()) {
        SDL_Texture* inputTexture = createTextTexture(displayText, fontMono, {255, 255, 255, 255});
        if (inputTexture) {
            float w, h;
            SDL_GetTextureSize(inputTexture, &w, &h);
            SDL_FRect textRect = {
                gotoInputRect.x + 5,
                gotoInputRect.y + (gotoInputRect.h - h) / 2,
                w, h
            };
            SDL_RenderTexture(renderer, inputTexture, nullptr, &textRect);
            SDL_DestroyTexture(inputTexture);
        }
    }
    
    // Goto button
    SDL_SetRenderDrawColor(renderer, 70, 130, 180, 255);
    SDL_RenderFillRect(renderer, &gotoButtonRect);
    SDL_SetRenderDrawColor(renderer, 100, 160, 210, 255);
    SDL_RenderRect(renderer, &gotoButtonRect);
    
    SDL_Texture* gotoText = createTextTexture("Go", fontUI, {255, 255, 255, 255});
    if (gotoText) {
        float w, h;
        SDL_GetTextureSize(gotoText, &w, &h);
        SDL_FRect textRect = {
            gotoButtonRect.x + (gotoButtonRect.w - w) / 2,
            gotoButtonRect.y + (gotoButtonRect.h - h) / 2,
            w, h
        };
        SDL_RenderTexture(renderer, gotoText, nullptr, &textRect);
        SDL_DestroyTexture(gotoText);
    }
    
    // Cancel button
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderFillRect(renderer, &gotoCancelButtonRect);
    SDL_SetRenderDrawColor(renderer, 110, 110, 110, 255);
    SDL_RenderRect(renderer, &gotoCancelButtonRect);
    
    SDL_Texture* cancelText = createTextTexture("Cancel", fontUI, {255, 255, 255, 255});
    if (cancelText) {
        float w, h;
        SDL_GetTextureSize(cancelText, &w, &h);
        SDL_FRect textRect = {
            gotoCancelButtonRect.x + (gotoCancelButtonRect.w - w) / 2,
            gotoCancelButtonRect.y + (gotoCancelButtonRect.h - h) / 2,
            w, h
        };
        SDL_RenderTexture(renderer, cancelText, nullptr, &textRect);
        SDL_DestroyTexture(cancelText);
    }
}