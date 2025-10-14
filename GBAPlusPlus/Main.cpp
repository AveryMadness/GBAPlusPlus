#include <iostream>
#include <SDL3/SDL.h>
#include <fstream>

#include "Windows.h"

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

    const std::string biosFilename = fileDialog("BIOS File (.bin)\0*.bin");

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

    const std::string romName = fileDialog("GBA ROM File (.gba)\0*.gba");

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

    bool running = true;
    SDL_Event event;

    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            registerWindow.handleEvents(event);
            memViewer.handleEvents(event);

            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                // Close main window ends program
                if (event.window.windowID == SDL_GetWindowID(window))
                    running = false;
            }
        }

        if (registerWindow.isWindowOpen())
            registerWindow.updateText(), registerWindow.render();

        if (memViewer.isWindowOpen())
            memViewer.render();

        SDL_Delay(16);
    }

    // Only destroy the main dummy window here
    SDL_Quit();
    TTF_Quit();
    
    return 0;
}
