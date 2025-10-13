#include "RegisterWindow.h"
#include <iostream>

RegisterWindow::RegisterWindow(const char* title, int x, int y, int width, int height)
    : window(nullptr), renderer(nullptr), font(nullptr), textTexture(nullptr), isOpen(true)
{
    window = SDL_CreateWindow(title, width, height, 0);
    if (!window)
    {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        isOpen = false;
        return;
    }

    SDL_SetWindowPosition(window, x, y);

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
        isOpen = false;
        return;
    }

    font = TTF_OpenFont("Assets/FOT-ChiaroStdB-Regular.otf", 24);
    if (!font)
    {
        std::cerr << "Failed to load font: " << SDL_GetError() << std::endl;
    }
}

RegisterWindow::~RegisterWindow()
{
    if (textTexture) SDL_DestroyTexture(textTexture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (font) TTF_CloseFont(font);
    if (window) SDL_DestroyWindow(window);
}

void RegisterWindow::setText(const std::string& text, SDL_Color color)
{
    if (!font) return;

    if (textTexture)
    {
        SDL_DestroyTexture(textTexture);
        textTexture = nullptr;
    }

    SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), 0, color);

    if (surface)
    {
        textTexture = SDL_CreateTextureFromSurface(renderer, surface);
        textRect = {50, 50, (float)surface->w, (float)surface->h};
        SDL_DestroySurface(surface);
    }
}

void RegisterWindow::render()
{
    if (!isOpen) return;

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    if (textTexture)
    {
        SDL_RenderTexture(renderer, textTexture, nullptr, &textRect);
    }

    SDL_RenderPresent(renderer);
}

void RegisterWindow::handleEvents(SDL_Event& event)
{
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        if (event.window.windowID == SDL_GetWindowID(window))
        {
            isOpen = false;
        }
    }
}

SDL_WindowID RegisterWindow::getWindowId() const
{
    return SDL_GetWindowID(window);
}






