#include <chrono>
#include <iostream>
#include <SDL3/SDL.h>
#include <fstream>

#include "Windows.h"
#include "AGB/ARM7TDMI.h"

#include "AGB/MemoryBus.h"
#include "UI/MemoryViewerWindow.h"
#include "UI/RegisterWindow.h"

std::string fileDialog(LPCSTR fileType) {
    OPENFILENAMEA ofn;
    char szFile[300];
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nFilterIndex = 1;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = fileType;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        return ofn.lpstrFile;
    }
    return "";
}

static constexpr int CYCLES_PER_FRAME = 280896;
static constexpr double TARGET_FPS = 59.73;
static constexpr double FRAME_TIME = 1.0 / TARGET_FPS;
static constexpr bool USE_FRAMERATE_CONTROL = false;  // Set to true to enable framerate, false for manual stepping

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

    ARMRegisters* registers = new ARMRegisters();
    registers->GetProgramStatusRegister().SetIRQDisable(true);
    registers->GetProgramStatusRegister().SetFIQDisable(true);
    registers->GetProgramStatusRegister().SetMode(Supervisor);

    RegisterWindow registerWindow = RegisterWindow("Registers", 100, 100, 1200, 900, registers);

    MemoryBus* memoryBus = new MemoryBus();
    MemoryViewerWindow memViewer("Memory Viewer", 100, 100, 1000, 600);
    memViewer.setMemoryBus(memoryBus);

    const std::string biosFilename = R"(C:\Users\Avery\Desktop\GBAEmu\x64\Debug\Assets\gba_bios.bin)";//fileDialog("BIOS File (.bin)\0*.bin");

    std::ifstream file(biosFilename, std::ios::binary | std::ios::ate);

    if (!file.is_open())
    {
        std::cerr << "Failed to open bios file.";
        SDL_Quit();
        return 1;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> biosBuffer(size);

    if (file.read(biosBuffer.data(), size))
    {
        std::cout << "Read BIOS data. Size: " << size << " bytes." << std::endl;
    }

    uint8_t* biosData = new uint8_t[size];
    memcpy(biosData, biosBuffer.data(), size);

    memoryBus->loadBIOS(biosData, size);

    const std::string romName =
        R"(C:\Users\Avery\Downloads\Pokemon - Emerald Version (USA, Europe)\Pokemon - Emerald Version (USA, Europe).gba)";//fileDialog("GBA ROM File (.gba)\0*.gba");

    std::ifstream romFile(biosFilename, std::ios::binary | std::ios::ate);

    if (!romFile.is_open())
    {
        std::cerr << "Failed to open bios file.";
        SDL_Quit();
        return 1;
    }

    std::streamsize romSize = romFile.tellg();
    romFile.seekg(0, std::ios::beg);

    std::vector<char> romBuffer(romSize);

    if (romFile.read(romBuffer.data(), romSize))
    {
        std::cout << "Read ROM data. Size: " << romSize << " bytes." << std::endl;
    }

    uint8_t* romData = new uint8_t[romSize];
    memcpy(romData, romBuffer.data(), romSize);

    memoryBus->loadROM(romData, romSize);

    ARM7TDMI* cpu = new ARM7TDMI(memoryBus, registers);
    cpu->InitializeCpuForExecution();

    bool running = true;
    SDL_Event event;
    bool stepPressed = false;

    using clock = std::chrono::high_resolution_clock;
    auto lastTime = clock::now();
    double accumulator = 0.0;

    std::cout << "Controls:" << std::endl;
    std::cout << "  SPACE - Step one instruction" << std::endl;
    std::cout << "  ESC   - Exit" << std::endl;

    while (running)
    {
        auto currentTime = clock::now();
        double deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
        lastTime = currentTime;
            
        accumulator += deltaTime;
        
        while (SDL_PollEvent(&event))
        {
            registerWindow.handleEvents(event);
            memViewer.handleEvents(event);

            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                if (event.window.windowID == SDL_GetWindowID(window))
                    running = false;
            }

            // Handle keyboard input for stepping
            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_SPACE)
                {
                    stepPressed = true;
                }
                else if (event.key.key == SDLK_ESCAPE)
                {
                    running = false;
                }
            }
        }

        if (USE_FRAMERATE_CONTROL) {
            // Framerate-controlled execution
            while (accumulator >= FRAME_TIME) {
                for (int i = 0; i < CYCLES_PER_FRAME; i++) {
                    cpu->runCpuStep();
                }
                accumulator -= FRAME_TIME;
            }
        } else {
            // Manual stepping
            if (stepPressed) {
                cpu->runCpuStep();
                stepPressed = false;
            }
        }

        if (registerWindow.isWindowOpen())
            registerWindow.updateText(), registerWindow.render();

        if (memViewer.isWindowOpen())
            memViewer.render();

        SDL_Delay(1);
    }

    // Only destroy the main dummy window here
    SDL_Quit();
    TTF_Quit();
    
    return 0;
}