// EmulatorApp.h
#pragma once
#include <wx/wx.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "../AGB/ARM7TDMI.h"
#include "../AGB/MemoryBus.h"
#include "../AGB/ARMRegisters.h"
#include "InputMap.h"

class SDLPanel;
class RegisterFrame;
class MemoryViewerFrame;

class EmulatorFrame : public wxFrame {
public:
    EmulatorFrame();
    ~EmulatorFrame();
    
private:
    void OnOpen(wxCommandEvent& event);
    void OnUnloadROM(wxCommandEvent& event);
    void OnLoadBIOS(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnStep(wxCommandEvent& event);
    void OnRun(wxCommandEvent& event);
    void OnPause(wxCommandEvent& event);
    void OnReset(wxCommandEvent& event);
    void OnShowRegisters(wxCommandEvent& event);
    void OnShowMemory(wxCommandEvent& event);
    void OnToggleTrace(wxCommandEvent& event);
    void OnDumpPPUState(wxCommandEvent& event);
    void OnDumpFrameImage(wxCommandEvent& event);
    void OnToggleFpsCounter(wxCommandEvent& event);
    void OnConfigureInput(wxCommandEvent& event);
    
    void PollInput();
    InputMap inputMap;

    void InitializeEmulator();
    void LoadBIOSFile(const wxString& path);
    void LoadROMFile(const wxString& path);
    void UpdateDebugWindows();
    
    void ResetEmulatorState();
    
    void EmulationThreadFunc();

    void InitAudio();
    void ShutdownAudio();
    void PumpAudio();
    void FlushAudio();

    SDL_AudioStream* audioStream = nullptr;

    //one emulated frame is about 549 stereo frames at the apus output rate
    static constexpr size_t AUDIO_SCRATCH_FRAMES = 4096;
    std::vector<int16_t> audioScratch;

    void OnFrameComplete();
    
    void LogFrameTiming(double stepSeconds);
    
    std::atomic<double> emulationFps{0.0};
    std::chrono::steady_clock::time_point fpsWindowStart;
    uint64_t fpsFrameCount = 0;

    std::atomic<bool> framePending{false};

    std::ofstream perfLog;
    std::chrono::steady_clock::time_point perfWindowStart;
    uint64_t perfFrameCount = 0;
    uint64_t perfOverBudgetCount = 0;
    double perfBusyTimeSum = 0.0;
    double perfMaxStepSeconds = 0.0;

    SDLPanel* sdlPanel;

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

    //background emulation thread
    std::thread emuThread;
    std::mutex emuMutex;
    std::atomic<bool> stopRequested{false};

    //if we dont use this the entire program will fucking implode the second you try to do anything useful
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>>(true);

    // Timing
    static constexpr int CYCLES_PER_FRAME = 280896;
    static constexpr double TARGET_FPS = 59.73;
    static constexpr double FRAME_TIME = 1.0 / TARGET_FPS;

    wxDECLARE_EVENT_TABLE();
};

class SDLPanel : public wxPanel {
public:
    SDLPanel(wxWindow* parent);
    ~SDLPanel();

    void InitSDL();
    void Render();

    void SetSource(MemoryBus* memoryBus, std::mutex* memoryMutex);

    void SetShowFps(bool show);
    void SetFpsSource(const std::atomic<double>* fps);

private:
    void RenderFpsCounter(int panelWidth);

    SDL_Window* sdlWindow;
    SDL_Renderer* sdlRenderer;
    SDL_Texture* gbaTexture;

    MemoryBus* memoryBus;
    std::mutex* memoryMutex;

    bool showFps = true;
    TTF_Font* fpsFont = nullptr;
    SDL_Texture* fpsTexture = nullptr;
    int fpsTextureWidth = 0;
    int fpsTextureHeight = 0;
    int lastDisplayedFps = -1;
    const std::atomic<double>* fpsSource = nullptr;
};

class EmulatorApp : public wxApp {
public:
    virtual bool OnInit() override;
    virtual int OnExit() override;
};