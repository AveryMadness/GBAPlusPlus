// EmulatorApp.h
#pragma once
#include <wx/wx.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <chrono>
#include "../AGB/ARM7TDMI.h"
#include "../AGB/MemoryBus.h"
#include "../AGB/ARMRegisters.h"

class SDLPanel;
class RegisterFrame;
class MemoryViewerFrame;

class EmulatorFrame : public wxFrame {
public:
    EmulatorFrame();
    ~EmulatorFrame();
    
private:
    void OnOpen(wxCommandEvent& event);
    void OnLoadBIOS(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnStep(wxCommandEvent& event);
    void OnRun(wxCommandEvent& event);
    void OnPause(wxCommandEvent& event);
    void OnReset(wxCommandEvent& event);
    void OnShowRegisters(wxCommandEvent& event);
    void OnShowMemory(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnIdle(wxIdleEvent& event);
    
    void InitializeEmulator();
    void LoadBIOSFile(const wxString& path);
    void LoadROMFile(const wxString& path);
    void UpdateDebugWindows();
    
    SDLPanel* sdlPanel;
    wxTimer* frameTimer;
    
    // Emulator components
    ARM7TDMI* cpu;
    MemoryBus* memoryBus;
    ARMRegisters* registers;
    
    // Debug windows
    RegisterFrame* registerWindow;
    MemoryViewerFrame* memoryWindow;
    
    // Emulation state
    bool isRunning;
    bool isPaused;
    bool biosLoaded;
    bool romLoaded;
    
    // Timing
    static constexpr int CYCLES_PER_FRAME = 280896;
    static constexpr double TARGET_FPS = 59.73;
    static constexpr double FRAME_TIME = 1.0 / TARGET_FPS;
    
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    double accumulator;
    
    wxDECLARE_EVENT_TABLE();
};

class SDLPanel : public wxPanel {
public:
    SDLPanel(wxWindow* parent);
    ~SDLPanel();
    
    void InitSDL();
    void Render();
    
private:
    SDL_Window* sdlWindow;
    SDL_Renderer* sdlRenderer;
};

class EmulatorApp : public wxApp {
public:
    virtual bool OnInit() override;
    virtual int OnExit() override;
};