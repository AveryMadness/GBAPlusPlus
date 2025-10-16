#include "RegisterWindow.h"
#include <sstream>
#include <iostream>

RegisterWindow::RegisterWindow(const char* title, int x, int y, int width, int height, ARMRegisters* regs)
    : window(nullptr), renderer(nullptr), font(nullptr), textTexture(nullptr), isOpen(true), registers(regs)
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

    font = TTF_OpenFont("Assets/cour.ttf", 20);
    if (!font)
    {
        std::cerr << "Failed to load font: " << SDL_GetError() << std::endl;
        isOpen = false;
        return;
    }
}

static const char* ModeToString(CPUMode mode)
{
    switch (mode)
    {
    case User: return "User";
    case FIQ: return "FIQ";
    case IRQ: return "IRQ";
    case Supervisor: return "Supervisor";
    case Abort: return "Abort";
    case Undefined: return "Undefined";
    case System: return "System";
    default: return "Unknown";
    }
}

RegisterWindow::~RegisterWindow()
{
    if (textTexture) SDL_DestroyTexture(textTexture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (font) TTF_CloseFont(font);
    if (window) SDL_DestroyWindow(window);
}

void RegisterWindow::updateText()
{
    if (!font || !registers) return;

    if (textTexture) SDL_DestroyTexture(textTexture);

    std::ostringstream oss;
    oss << std::hex;

    // General-purpose registers
    for (int i = 0; i <= 12; ++i)
    {
        uint32_t* reg = registers->GetRegister(i);
        oss << std::dec << "R" << i << ": 0x" << std::hex << *reg << "\n";
    }

    // Special registers
    oss << "SP: 0x" << *registers->GetRegister(STACK_POINTER) << "\n";
    oss << "LR: 0x" << *registers->GetRegister(LINK_REGISTER) << "\n";
    oss << "PC: 0x" << *registers->GetRegister(PROGRAM_COUNTER) - 8 << "\n";

    // CPSR
    ProgramStatusRegister psr = registers->GetProgramStatusRegister();
    
    CPUMode mode = psr.GetMode();
    oss << "Mode: " << ModeToString(mode) << "\n";
    
    oss << "CPSR: 0x" << psr.CPSR << "\n";

    ProgramStatusRegister spsr = registers->GetSavedProgramStatusRegister();

    oss << "SPSR: 0x" << spsr.CPSR << "\n";

    struct Flag { const char* name; bool value; };
    Flag flags[] = {
        {"N", psr.GetNegative()},
        {"Z", psr.GetZero()},
        {"C", psr.GetCarry()},
        {"V", psr.GetOverflow()},
        {"I", psr.GetIRQDisable()},
        {"F", psr.GetFIQDisable()},
        {"T", psr.GetThumbState()}
    };

    oss << "Flags: ";
    for (auto& f : flags)
    {
        if (f.value) oss << f.name;   // set flags
        else oss << "-";              // clear flags
        oss << " ";
    }
    oss << "\n";

    // Render all text first (without colored flags)
    SDL_Surface* surface = TTF_RenderText_Solid_Wrapped(font, oss.str().c_str(), 0, {0,0,0,255}, 400);
    if (!surface) return;

    textTexture = SDL_CreateTextureFromSurface(renderer, surface);
    textRect = {10, 10, (float)surface->w, (float)surface->h};
    SDL_DestroySurface(surface);
}

void RegisterWindow::render()
{
    if (!isOpen) return;

    // Clear window
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    // Draw the pre-rendered text
    if (textTexture)
        SDL_RenderTexture(renderer, textTexture, nullptr, &textRect);

    SDL_RenderPresent(renderer);
}

void RegisterWindow::handleEvents(SDL_Event& event)
{
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        if (event.window.windowID == SDL_GetWindowID(window))
            isOpen = false;
    }
}

SDL_WindowID RegisterWindow::getWindowId() const
{
    return SDL_GetWindowID(window);
}
