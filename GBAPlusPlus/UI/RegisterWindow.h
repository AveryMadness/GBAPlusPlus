#pragma once
#include <string>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "../AGB/ARMRegisters.h"

class RegisterWindow
{
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    SDL_Texture* textTexture;
    SDL_FRect textRect;
    bool isOpen;

    ARMRegisters* registers; // pointer to ARM registers

public:
    RegisterWindow(const char* title, int x, int y, int width, int height, ARMRegisters* regs);
    ~RegisterWindow();

    void updateText();   // updates the register display
    void render();       // renders the window
    void handleEvents(SDL_Event& event);
    bool isWindowOpen() const { return isOpen; }
    SDL_WindowID getWindowId() const;
};
