#pragma once
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// Forward declare MemoryBus
class MemoryBus;

struct MemoryRegion {
    std::string name;
    uint32_t startAddress;
    uint32_t size;
    SDL_Color color;
};

class MemoryViewerWindow {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* fontMono;
    TTF_Font* fontUI;
    bool isOpen;
    
    MemoryBus* memoryBus;
    
    // View state
    uint32_t viewOffset;        // Current scroll position
    int bytesPerRow;            // 16 is standard
    int visibleRows;
    
    // UI state
    int scrollY;
    bool isDraggingScrollbar;
    SDL_FRect scrollbarRect;
    SDL_FRect scrollbarThumbRect;
    
    // Goto dialog state
    bool showGotoDialog;
    std::string gotoInputText;
    SDL_FRect gotoDialogRect;
    SDL_FRect gotoInputRect;
    SDL_FRect gotoButtonRect;
    SDL_FRect gotoCancelButtonRect;
    bool gotoInputActive;
    
    // Selection
    uint32_t selectedAddress;
    bool hasSelection;
    
    // Layout
    int addressColumnWidth;
    int hexColumnWidth;
    int asciiColumnWidth;
    int rowHeight;
    int headerHeight;
    int padding;
    
    // Memory regions for coloring
    std::vector<MemoryRegion> regions;
    
    // Cached textures
    struct CachedRow {
        uint32_t address;
        SDL_Texture* addressTexture;
        SDL_Texture* hexTexture;
        SDL_Texture* asciiTexture;
        bool dirty;
    };
    std::vector<CachedRow> cachedRows;
    
public:
    MemoryViewerWindow(const char* title, int x, int y, int width, int height);
    ~MemoryViewerWindow();
    
    void setMemoryBus(MemoryBus* bus);
    void render();
    void handleEvents(SDL_Event& event);
    void update();
    
    void scrollTo(uint32_t address);
    void setSelection(uint32_t address);
    void clearCache();
    void showGoto() { showGotoDialog = true; gotoInputActive = true; }
    void hideGoto() { showGotoDialog = false; gotoInputText.clear(); }
    
    bool isWindowOpen() const { return isOpen; }
    SDL_WindowID getWindowId() const;
    
private:
    void initializeRegions();
    void calculateLayout();
    void renderHeader();
    void renderMemoryRows();
    void renderScrollbar();
    void renderRegionLabels();
    void renderGotoDialog();
    
    SDL_Color getColorForAddress(uint32_t address);
    std::string getRegionNameForAddress(uint32_t address);
    
    SDL_Texture* createTextTexture(const std::string& text, TTF_Font* font, SDL_Color color);
    void handleMouseWheel(const SDL_MouseWheelEvent& wheel);
    void handleMouseButton(const SDL_MouseButtonEvent& button);
    void handleMouseMotion(const SDL_MouseMotionEvent& motion);
    void handleKeyPress(const SDL_KeyboardEvent& key);
    void handleTextInput(const SDL_TextInputEvent& text);
    
    void executeGoto();
    uint32_t parseHexAddress(const std::string& text);
    
    uint32_t getAddressAtPosition(int x, int y);
    void updateScrollbarThumb();
    bool isPointInRect(float x, float y, const SDL_FRect& rect);
};