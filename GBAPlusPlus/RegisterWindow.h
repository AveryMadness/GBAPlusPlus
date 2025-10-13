#pragma once
#include <string>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

class RegisterWindow
{
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    SDL_Texture* textTexture;
    SDL_FRect textRect;
    bool isOpen;

public:
    RegisterWindow(const char* title, int x, int y, int width, int height);
    ~RegisterWindow();

    void setText(const std::string& text, SDL_Color color = {0, 0, 0, 255});
    void render();
    void handleEvents(SDL_Event& event);
    bool isWindowOpen() const { return isOpen; }
    SDL_WindowID getWindowId() const;
};
