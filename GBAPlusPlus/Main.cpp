#include <iostream>
#include <SDL3/SDL.h>

#include "RegisterWindow.h"

int main(int argc, char* argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (!TTF_Init())
    {
        std::cerr << "TTF_Init error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("GBA++", 800, 600, 0);

    if (!window)
    {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    RegisterWindow registerWindow = RegisterWindow("Registers", 100, 100, 400, 300);
    registerWindow.setText("Test");

    bool running = true;
    SDL_Event event;

    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                if (event.window.windowID == SDL_GetWindowID(window))
                {
                    running = false;
                }
            }

            registerWindow.handleEvents(event);
        }

        if (registerWindow.isWindowOpen())
        {
            registerWindow.render();
        }
        
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    
    return 0;
}
