// EmulatorApp.cpp
#include "EmulatorApp.h"
#include "RegisterFrame.h"
#include "MemoryViewerFrame.h"
#include "InputSettingsDialog.h"
#include <algorithm>
#include <fstream>
#include <vector>
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#define NOMINMAX
#include <Windows.h>
#pragma comment(lib, "Winmm.lib")

namespace {
    const wxString kBiosPathConfigKey = "/LastBiosPath";
    const wxString kRomPathConfigKey = "/LastRomPath";
}

enum {
    ID_LoadBIOS = wxID_HIGHEST + 1,
    ID_UnloadROM,
    ID_Step,
    ID_Run,
    ID_Pause,
    ID_Reset,
    ID_ShowRegisters,
    ID_ShowMemory,
    ID_ToggleTrace,
    ID_DumpPPUState,
    ID_DumpFrameImage,
    ID_ToggleFpsCounter,
    ID_ConfigureInput
};

wxBEGIN_EVENT_TABLE(EmulatorFrame, wxFrame)
    EVT_MENU(wxID_OPEN, EmulatorFrame::OnOpen)
    EVT_MENU(ID_UnloadROM, EmulatorFrame::OnUnloadROM)
    EVT_MENU(ID_LoadBIOS, EmulatorFrame::OnLoadBIOS)
    EVT_MENU(wxID_EXIT, EmulatorFrame::OnExit)
    EVT_MENU(ID_Step, EmulatorFrame::OnStep)
    EVT_MENU(ID_Run, EmulatorFrame::OnRun)
    EVT_MENU(ID_Pause, EmulatorFrame::OnPause)
    EVT_MENU(ID_Reset, EmulatorFrame::OnReset)
    EVT_MENU(ID_ShowRegisters, EmulatorFrame::OnShowRegisters)
    EVT_MENU(ID_ShowMemory, EmulatorFrame::OnShowMemory)
    EVT_MENU(ID_ToggleTrace, EmulatorFrame::OnToggleTrace)
    EVT_MENU(ID_DumpPPUState, EmulatorFrame::OnDumpPPUState)
    EVT_MENU(ID_DumpFrameImage, EmulatorFrame::OnDumpFrameImage)
    EVT_MENU(ID_ToggleFpsCounter, EmulatorFrame::OnToggleFpsCounter)
    EVT_MENU(ID_ConfigureInput, EmulatorFrame::OnConfigureInput)
wxEND_EVENT_TABLE()

bool EmulatorApp::OnInit() {
    SetAppName("GBAPlusPlus");
    SetVendorName("GBAPlusPlus");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        wxLogError("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!TTF_Init()) {
        wxLogError("TTF_Init failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    EmulatorFrame* frame = new EmulatorFrame();
    frame->Show(true);
    return true;
}

int EmulatorApp::OnExit() {
    TTF_Quit();
    SDL_Quit();
    return wxApp::OnExit();
}

EmulatorFrame::EmulatorFrame()
    : wxFrame(nullptr, wxID_ANY, "GBA++ Emulator", wxDefaultPosition, wxSize(900, 700))
    , cpu(nullptr)
    , memoryBus(nullptr)
    , registers(nullptr)
    , registerWindow(nullptr)
    , memoryWindow(nullptr)
    , isRunning(false)
    , isPaused(false)
    , biosLoaded(false)
    , romLoaded(false)
{
    inputMap.LoadFromConfig();

    // Menu bar
    wxMenuBar* menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append(ID_LoadBIOS, "Load &BIOS...\tCtrl-B", "Load GBA BIOS file");
    fileMenu->Append(wxID_OPEN, "&Open ROM...\tCtrl-O", "Load GBA ROM file");
    fileMenu->Append(ID_UnloadROM, "&Unload ROM\tCtrl-Shift-U", "Eject the cartridge and return to BIOS-only");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit\tAlt-F4", "Exit the emulator");
    menuBar->Append(fileMenu, "&File");

    wxMenu* emuMenu = new wxMenu();
    emuMenu->Append(ID_Step,  "&Step\tSpace",   "Execute one instruction");
    emuMenu->Append(ID_Run,   "&Run\tF5",        "Run emulation");
    emuMenu->Append(ID_Pause, "&Pause\tF6",      "Pause emulation");
    emuMenu->AppendSeparator();
    emuMenu->Append(ID_Reset, "R&eset\tCtrl-R", "Reset emulator");
    emuMenu->AppendSeparator();
    emuMenu->Append(ID_ConfigureInput, "Configure &Input...\tCtrl-I",
        "Choose which keys drive the GBA buttons");
    menuBar->Append(emuMenu, "&Emulation");

    wxMenu* viewMenu = new wxMenu();
    wxMenuItem* fpsCounterItem = viewMenu->AppendCheckItem(ID_ToggleFpsCounter, "Show &FPS Counter",
        "Overlay the current framerate in the top-right corner of the display");
    fpsCounterItem->Check(true);
    menuBar->Append(viewMenu, "&View");

    wxMenu* debugMenu = new wxMenu();
    debugMenu->Append(ID_ShowRegisters, "Show &Registers\tCtrl-Shift-R", "Show register window");
    debugMenu->Append(ID_ShowMemory,    "Show &Memory\tCtrl-Shift-M",    "Show memory viewer");
    debugMenu->AppendSeparator();
    debugMenu->Append(ID_ToggleTrace, "Start Instruction &Trace...\tCtrl-Shift-T",
        "Record an address/opcode/register trace to a file for comparison against a reference");
    debugMenu->Append(ID_DumpPPUState, "Dump &PPU/OAM State\tCtrl-Shift-P",
        "Write DISPCNT/window/BG registers and OAM sprite entries to a text file");
    debugMenu->Append(ID_DumpFrameImage, "Dump Frame as &Image\tCtrl-Shift-I",
        "Save the current rendered frame as a BMP for visual inspection");
    menuBar->Append(debugMenu, "&Debug");

    SetMenuBar(menuBar);

    CreateStatusBar(2);
    SetStatusText("Ready - Load BIOS and ROM to begin", 0);
    SetStatusText("Stopped", 1);

    wxPanel* mainPanel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    sdlPanel = new SDLPanel(mainPanel);
    sdlPanel->SetMinSize(wxSize(240 * 3, 160 * 3));
    mainSizer->Add(sdlPanel, 1, wxEXPAND | wxALL, 5);

    wxBoxSizer* controlSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* stepBtn  = new wxButton(mainPanel, ID_Step,  "Step");
    wxButton* runBtn   = new wxButton(mainPanel, ID_Run,   "Run");
    wxButton* pauseBtn = new wxButton(mainPanel, ID_Pause, "Pause");
    wxButton* resetBtn = new wxButton(mainPanel, ID_Reset, "Reset");
    controlSizer->Add(stepBtn,  0, wxALL, 5);
    controlSizer->Add(runBtn,   0, wxALL, 5);
    controlSizer->Add(pauseBtn, 0, wxALL, 5);
    controlSizer->Add(resetBtn, 0, wxALL, 5);
    mainSizer->Add(controlSizer, 0, wxALIGN_CENTER);

    mainPanel->SetSizer(mainSizer);

    InitializeEmulator();

    wxString savedBiosPath, savedRomPath;
    wxConfigBase* config = wxConfigBase::Get();
    bool haveBios = config->Read(kBiosPathConfigKey, &savedBiosPath) && wxFileExists(savedBiosPath);
    bool haveRom  = config->Read(kRomPathConfigKey, &savedRomPath) && wxFileExists(savedRomPath);

    if (haveBios)
        LoadBIOSFile(savedBiosPath);
    if (haveRom)
        LoadROMFile(savedRomPath);
}

EmulatorFrame::~EmulatorFrame() {
    //must be first: lets any already-queued CallAfter callback (from the last
    //loop iteration before stopRequested was observed) detect we're gone
    aliveFlag->store(false);

    stopRequested = true;
    if (emuThread.joinable())
        emuThread.join();

    registerWindow = nullptr;
    memoryWindow   = nullptr;

    delete cpu;
    delete memoryBus;
    delete registers;
}

void EmulatorFrame::InitializeEmulator() {
    registers = new ARMRegisters();
    registers->GetProgramStatusRegister().SetIRQDisable(true);
    registers->GetProgramStatusRegister().SetFIQDisable(true);
    registers->GetProgramStatusRegister().SetMode(Supervisor);

    memoryBus = new MemoryBus();
    cpu = new ARM7TDMI(memoryBus, registers);

    sdlPanel->SetSource(memoryBus, &emuMutex);

    wxString perfLogPath = wxStandardPaths::Get().GetDocumentsDir()
        + wxFileName::GetPathSeparator() + "gbaplusplus_perf.log";
    perfLog.open(perfLogPath.ToStdString(), std::ios::out | std::ios::trunc);
    if (perfLog.is_open())
        perfLog << "budget per frame = " << (FRAME_TIME * 1000.0) << "ms ("
                << CYCLES_PER_FRAME << " cycles @ " << TARGET_FPS << " fps)\n";
    perfWindowStart = std::chrono::steady_clock::now();
}

void EmulatorFrame::OnLoadBIOS(wxCommandEvent& event) {
    wxFileDialog dlg(this, "Load GBA BIOS", "", "",
                     "BIOS files (*.bin)|*.bin|All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_CANCEL)
        LoadBIOSFile(dlg.GetPath());
}

void EmulatorFrame::LoadBIOSFile(const wxString& path) {
    std::ifstream file(path.ToStdString(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        wxMessageBox("Failed to open BIOS file", "Error", wxICON_ERROR);
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size != 16384) {
        wxMessageBox("Invalid BIOS file size (should be 16KB)", "Error", wxICON_ERROR);
        return;
    }

    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        {
            std::lock_guard<std::mutex> lock(emuMutex);
            memoryBus->loadBIOS(buffer.data(), size);
        }
        biosLoaded = true;
        wxConfigBase::Get()->Write(kBiosPathConfigKey, path);
        ResetEmulatorState();
        SetStatusText("BIOS loaded: " + path, 0);
    } else {
        wxMessageBox("Failed to read BIOS file", "Error", wxICON_ERROR);
    }
}

void EmulatorFrame::OnOpen(wxCommandEvent& event) {
    wxFileDialog dlg(this, "Open GBA ROM", "", "",
                     "GBA ROMs (*.gba)|*.gba|All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_CANCEL)
        LoadROMFile(dlg.GetPath());
}

void EmulatorFrame::LoadROMFile(const wxString& path) {
    std::ifstream file(path.ToStdString(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        wxMessageBox("Failed to open ROM file", "Error", wxICON_ERROR);
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        {
            std::lock_guard<std::mutex> lock(emuMutex);
            memoryBus->loadROM(buffer.data(), size);
        }
        romLoaded = true;
        wxConfigBase::Get()->Write(kRomPathConfigKey, path);
        SetStatusText("ROM loaded: " + path, 0);
        if (biosLoaded) {
            ResetEmulatorState();
            SetStatusText("Ready to run", 0);
        } else {
            SetStatusText("Load BIOS to continue", 0);
        }
    } else {
        wxMessageBox("Failed to read ROM file", "Error", wxICON_ERROR);
    }
}

void EmulatorFrame::OnUnloadROM(wxCommandEvent& event) {
    if (!romLoaded) return;

    if (isRunning) {
        stopRequested = true;
        if (emuThread.joinable())
            emuThread.join();
        isRunning = false;
        isPaused = false;
    }

    {
        std::lock_guard<std::mutex> lock(emuMutex);
        memoryBus->unloadROM();
    }
    romLoaded = false;
    wxConfigBase::Get()->DeleteEntry(kRomPathConfigKey);

    ResetEmulatorState();
    SetStatusText("ROM unloaded - running BIOS only", 0);
    SetStatusText("Stopped", 1);
    UpdateDebugWindows();
}

void EmulatorFrame::OnExit(wxCommandEvent& event) {
    Close(true);
}

void EmulatorFrame::OnStep(wxCommandEvent& event) {
    if (!biosLoaded) {
        wxMessageBox("Please load a BIOS first", "Not Ready", wxICON_WARNING);
        return;
    }
    if (isRunning) return; // use Pause before single-stepping

    if (cpu) {
        uint32_t pc;
        try
        {
            std::lock_guard<std::mutex> lock(emuMutex);
            cpu->runCpuStep();
            pc = *registers->GetRegister(PROGRAM_COUNTER);
        }
        catch (const std::exception& e)
        {
            wxMessageBox(e.what(), "CPU Error", wxICON_ERROR);
            return;
        }
        UpdateDebugWindows();
        SetStatusText(wxString::Format("PC: 0x%08X", pc), 1);
    }
}

void EmulatorFrame::OnRun(wxCommandEvent& event) {
    if (!biosLoaded) {
        wxMessageBox("Please load a BIOS first", "Not Ready", wxICON_WARNING);
        return;
    }
    if (isRunning) return;

    isRunning = true;
    isPaused  = false;
    stopRequested = false;
    SetStatusText("Running", 1);

    emuThread = std::thread(&EmulatorFrame::EmulationThreadFunc, this);
}

void EmulatorFrame::OnPause(wxCommandEvent& event) {
    if (!isRunning) return;

    stopRequested = true;
    if (emuThread.joinable())
        emuThread.join();

    isRunning = false;
    isPaused  = true;
    SetStatusText("Paused", 1);
    UpdateDebugWindows();
}

void EmulatorFrame::OnReset(wxCommandEvent& event) {
    if (isRunning) {
        stopRequested = true;
        if (emuThread.joinable())
            emuThread.join();
    }
    isRunning = false;
    isPaused  = false;

    ResetEmulatorState();
    SetStatusText("Reset complete - Ready to run", 0);
    SetStatusText("Stopped", 1);
    UpdateDebugWindows();
}

void EmulatorFrame::ResetEmulatorState() {
    if (!biosLoaded)
        return;

    std::lock_guard<std::mutex> lock(emuMutex);
    memoryBus->reset();
    registers->Reset();
    registers->GetProgramStatusRegister().SetIRQDisable(true);
    registers->GetProgramStatusRegister().SetFIQDisable(true);
    registers->GetProgramStatusRegister().SetMode(Supervisor);
    cpu->InitializeCpuForExecution();
}

void EmulatorFrame::OnShowRegisters(wxCommandEvent& event) {
    if (!registerWindow) {
        registerWindow = new RegisterFrame(this, registers, memoryBus, &emuMutex);
    }
    registerWindow->Show();
    registerWindow->Raise();
}

void EmulatorFrame::OnShowMemory(wxCommandEvent& event) {
    if (!memoryWindow) {
        memoryWindow = new MemoryViewerFrame(this, memoryBus, &emuMutex);
    }
    memoryWindow->Show();
    memoryWindow->Raise();
}

void EmulatorFrame::OnToggleTrace(wxCommandEvent& event) {
    if (!cpu) return;

    if (cpu->IsTracing()) {
        std::lock_guard<std::mutex> lock(emuMutex);
        cpu->DisableTracing();
        SetStatusText("Trace stopped", 0);
        return;
    }

    if (isRunning) {
        wxMessageBox("Pause emulation before starting an instruction trace.", "Not Ready", wxICON_WARNING);
        return;
    }

    wxFileDialog dlg(this, "Save Instruction Trace", "", "trace.log",
                     "Log files (*.log)|*.log|Text files (*.txt)|*.txt|All files (*.*)|*.*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_CANCEL) return;

    bool started;
    {
        std::lock_guard<std::mutex> lock(emuMutex);
        started = cpu->EnableTracing(dlg.GetPath().ToStdString());
    }

    if (!started) {
        wxMessageBox("Could not open that file for writing.", "Trace Not Started", wxICON_ERROR);
        return;
    }

    SetStatusText("Tracing to " + dlg.GetPath(), 0);
}

void EmulatorFrame::OnDumpPPUState(wxCommandEvent& event) {
    if (!memoryBus) return;

    wxString path = wxStandardPaths::Get().GetDocumentsDir() + wxFileName::GetPathSeparator() + "gbaplusplus_ppu_dump.txt";
    {
        std::lock_guard<std::mutex> lock(emuMutex);
        memoryBus->DumpDebugState(path.ToStdString());
    }
    SetStatusText("PPU state dumped to " + path, 0);
}

void EmulatorFrame::OnDumpFrameImage(wxCommandEvent& event) {
    if (!memoryBus) return;

    wxString path = wxStandardPaths::Get().GetDocumentsDir() + wxFileName::GetPathSeparator() + "gbaplusplus_frame.bmp";
    {
        std::lock_guard<std::mutex> lock(emuMutex);
        memoryBus->SaveFrameAsBMP(path.ToStdString());
    }
    SetStatusText("Frame image saved to " + path, 0);
}

void EmulatorFrame::OnToggleFpsCounter(wxCommandEvent& event) {
    sdlPanel->SetShowFps(event.IsChecked());
}

void EmulatorFrame::EmulationThreadFunc() {
    //different thread, avoid ui lag
    //windows fucking SUCKS so i have to call ts
    timeBeginPeriod(1);

    auto lastTime = std::chrono::steady_clock::now();
    double accumulator = 0.0;

    while (!stopRequested.load(std::memory_order_relaxed)) {
        auto   currentTime = std::chrono::steady_clock::now();
        double deltaTime    = std::chrono::duration<double>(currentTime - lastTime).count();
        lastTime = currentTime;

        //if we dont do this we FUCKING DIE.
        if (deltaTime > 0.1) deltaTime = 0.1;
        accumulator += deltaTime;

        bool ranAFrame = false;
        bool hadError = false;
        std::string errorMessage;

        while (accumulator >= FRAME_TIME) {
            auto stepStart = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(emuMutex);
                //use real cycle cost
                uint64_t targetCycles = cpu->GetTotalCycles() + CYCLES_PER_FRAME;
                while (cpu->GetTotalCycles() < targetCycles)
                {
                    try
                    {
                        cpu->runCpuStep();
                    }
                    catch (const std::exception& e)
                    {
                        hadError = true;
                        errorMessage = e.what();
                        break;
                    }
                }
            }
            auto stepEnd = std::chrono::steady_clock::now();
            LogFrameTiming(std::chrono::duration<double>(stepEnd - stepStart).count());

            accumulator -= FRAME_TIME;
            ranAFrame = true;
            if (hadError) break;

            //let the mutex like actually fucking work... please
            std::this_thread::yield();
        }

        if (hadError) {
            stopRequested.store(true, std::memory_order_relaxed);
            wxTheApp->CallAfter([this, errorMessage, alive = aliveFlag]() {
                if (!alive->load()) return;
                isRunning = false;
                isPaused  = true;
                SetStatusText("Stopped", 1);
                wxMessageBox(errorMessage, "CPU Error", wxICON_ERROR);
            });
            break;
        }

        if (ranAFrame) {
            wxTheApp->CallAfter([this, alive = aliveFlag]() {
                if (!alive->load()) return;
                OnFrameComplete();
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    timeEndPeriod(1);
}

void EmulatorFrame::LogFrameTiming(double stepSeconds) {
    perfFrameCount++;
    perfBusyTimeSum += stepSeconds;
    if (stepSeconds > perfMaxStepSeconds)
        perfMaxStepSeconds = stepSeconds;
    if (stepSeconds > FRAME_TIME)
        perfOverBudgetCount++;

    auto now = std::chrono::steady_clock::now();
    double windowElapsed = std::chrono::duration<double>(now - perfWindowStart).count();
    if (windowElapsed < 0.5 || perfFrameCount == 0)
        return;

    if (perfLog.is_open()) {
        double budgetMs = FRAME_TIME * 1000.0;
        double avgMs = (perfBusyTimeSum / static_cast<double>(perfFrameCount)) * 1000.0;
        double maxMs = perfMaxStepSeconds * 1000.0;
        double achievedFps = static_cast<double>(perfFrameCount) / windowElapsed;

        perfLog << "frames=" << perfFrameCount
                << " avg=" << avgMs << "ms (" << (avgMs / budgetMs * 100.0) << "% of " << budgetMs << "ms budget)"
                << " max=" << maxMs << "ms"
                << " over_budget=" << perfOverBudgetCount << "/" << perfFrameCount
                << " ~fps=" << achievedFps
                << "\n";
        perfLog.flush();
    }

    perfFrameCount = 0;
    perfBusyTimeSum = 0.0;
    perfMaxStepSeconds = 0.0;
    perfOverBudgetCount = 0;
    perfWindowStart = now;
}

void EmulatorFrame::PollInput() {
    if (!memoryBus)
        return;

    Input& input = memoryBus->GetInput();

    //get input from OS rather than wx.... because idk its probably better?
    if (!IsActive()) {
        input.ReleaseAll();
        return;
    }

    uint16_t mask = 0;
    for (GbaButton button : InputMap::AllButtons()) {
        const int keyCode = inputMap.GetKeyFor(button);
        if (keyCode != InputMap::UNBOUND && wxGetKeyState(static_cast<wxKeyCode>(keyCode)))
            mask |= static_cast<uint16_t>(1u << static_cast<uint8_t>(button));
    }

    input.SetPressedMask(mask);
}

void EmulatorFrame::OnConfigureInput(wxCommandEvent& event) {
    InputSettingsDialog dialog(this, inputMap);
    if (dialog.ShowModal() != wxID_OK)
        return;

    inputMap = dialog.GetResult();
    inputMap.SaveToConfig();
    SetStatusText("Input bindings updated", 0);
}

void EmulatorFrame::OnFrameComplete() {
    //runs on the UI thread via CallAfter.
    PollInput();
    sdlPanel->Render();

    static int frameCount = 0;
    if (++frameCount >= 10) {
        frameCount = 0;
        UpdateDebugWindows();

        uint32_t pc;
        {
            std::lock_guard<std::mutex> lock(emuMutex);
            pc = *registers->GetRegister(PROGRAM_COUNTER);
        }
        SetStatusText(wxString::Format("PC: 0x%08X", pc), 1);
    }
}

void EmulatorFrame::UpdateDebugWindows() {
    if (registerWindow && registerWindow->IsShown())
        registerWindow->UpdateDisplay();
    if (memoryWindow && memoryWindow->IsShown())
        memoryWindow->UpdateDisplay();
}

// ── SDLPanel ─────────────────────────────────────────────────────────────────

SDLPanel::SDLPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
    , sdlWindow(nullptr)
    , sdlRenderer(nullptr)
    , gbaTexture(nullptr)
    , memoryBus(nullptr)
    , memoryMutex(nullptr)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxBLACK);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (sdlWindow) {
            wxSize size = GetSize();
            SDL_SetWindowSize(sdlWindow, size.GetWidth(), size.GetHeight());
        }
        event.Skip();
    });

    CallAfter(&SDLPanel::InitSDL);
}

SDLPanel::~SDLPanel() {
    if (fpsTexture)  SDL_DestroyTexture(fpsTexture);
    if (fpsFont)     TTF_CloseFont(fpsFont);
    if (gbaTexture)  SDL_DestroyTexture(gbaTexture);
    if (sdlRenderer) SDL_DestroyRenderer(sdlRenderer);
    if (sdlWindow)   SDL_DestroyWindow(sdlWindow);
}

void SDLPanel::SetShowFps(bool show) {
    showFps = show;
}

void SDLPanel::SetSource(MemoryBus* bus, std::mutex* mutex) {
    memoryBus = bus;
    memoryMutex = mutex;
}

void SDLPanel::InitSDL() {
#ifdef __WXMSW__
    HWND hwnd = (HWND)GetHandle();
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, hwnd);
    sdlWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
#elif defined(__WXGTK__)
    Display* x11display = GDK_WINDOW_XDISPLAY(gtk_widget_get_window(GetHandle()));
    Window   x11window  = GDK_WINDOW_XID(gtk_widget_get_window(GetHandle()));
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_X11_DISPLAY_POINTER, x11display);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,    x11window);
    sdlWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
#elif defined(__WXMAC__)
    NSView* nsview = (NSView*)GetHandle();
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_COCOA_WINDOW_POINTER, [nsview window]);
    sdlWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
#endif

    if (!sdlWindow) {
        wxLogError("Failed to create SDL window: %s", SDL_GetError());
        return;
    }

    sdlRenderer = SDL_CreateRenderer(sdlWindow, nullptr);
    if (!sdlRenderer) {
        wxLogError("Failed to create SDL renderer: %s", SDL_GetError());
        return;
    }

    gbaTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, 240, 160);
    if (!gbaTexture) {
        wxLogError("Failed to create GBA framebuffer texture: %s", SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(gbaTexture, SDL_SCALEMODE_NEAREST);

    const char* basePath = SDL_GetBasePath();
    wxString fontPath = wxString(basePath ? basePath : "") + "Assets" + wxFileName::GetPathSeparator() + "NotoSans-Regular.ttf";
    fpsFont = TTF_OpenFont(fontPath.ToStdString().c_str(), 18.0f);
    if (!fpsFont)
        wxLogError("Failed to load FPS counter font (%s): %s", fontPath, SDL_GetError());
}

void SDLPanel::Render() {
    if (!sdlRenderer || !gbaTexture) return;

    static uint32_t pixels[240 * 160];

    if (memoryBus && memoryMutex) {
        std::lock_guard<std::mutex> lock(*memoryMutex);
        memoryBus->RenderFrame(pixels);
    }

    SDL_UpdateTexture(gbaTexture, nullptr, pixels, 240 * sizeof(uint32_t));

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);

    //keep the 240:160 aspect ratio, letterboxed to fit the panel
    int panelWidth, panelHeight;
    SDL_GetCurrentRenderOutputSize(sdlRenderer, &panelWidth, &panelHeight);

    float scale = std::min(panelWidth / 240.0f, panelHeight / 160.0f);
    if (scale <= 0.0f) scale = 1.0f;

    SDL_FRect destRect;
    destRect.w = 240 * scale;
    destRect.h = 160 * scale;
    destRect.x = (panelWidth - destRect.w) / 2.0f;
    destRect.y = (panelHeight - destRect.h) / 2.0f;

    SDL_RenderTexture(sdlRenderer, gbaTexture, nullptr, &destRect);

    //track real wall-clock time between rendered frames, smoothed so the
    //displayed number doesn't jitter frame to frame
    auto now = std::chrono::steady_clock::now();
    if (haveLastRenderTime) {
        double dt = std::chrono::duration<double>(now - lastRenderTime).count();
        if (dt > 0.0 && dt < 1.0) {
            double instantFps = 1.0 / dt;
            const double smoothing = 0.9;
            smoothedFps = (smoothedFps <= 0.0) ? instantFps : (smoothedFps * smoothing + instantFps * (1.0 - smoothing));
        }
    }
    lastRenderTime = now;
    haveLastRenderTime = true;

    if (showFps)
        RenderFpsCounter(panelWidth);

    SDL_RenderPresent(sdlRenderer);
}

void SDLPanel::RenderFpsCounter(int panelWidth) {
    if (!fpsFont) return;

    int displayedFps = static_cast<int>(smoothedFps + 0.5);

    if (displayedFps != lastDisplayedFps || !fpsTexture) {
        lastDisplayedFps = displayedFps;

        char text[32];
        snprintf(text, sizeof(text), "FPS: %d", displayedFps);

        SDL_Color color{255, 255, 0, 255};
        SDL_Surface* surface = TTF_RenderText_Blended(fpsFont, text, 0, color);
        if (!surface) return;

        if (fpsTexture) SDL_DestroyTexture(fpsTexture);
        fpsTexture = SDL_CreateTextureFromSurface(sdlRenderer, surface);
        fpsTextureWidth = surface->w;
        fpsTextureHeight = surface->h;
        SDL_DestroySurface(surface);
    }

    if (!fpsTexture) return;

    const float margin = 6.0f;
    SDL_FRect dest;
    dest.w = static_cast<float>(fpsTextureWidth);
    dest.h = static_cast<float>(fpsTextureHeight);
    dest.x = panelWidth - dest.w - margin;
    dest.y = margin;

    //dark backing rect so the yellow text stays legible over bright backgrounds
    SDL_FRect backing = dest;
    backing.x -= 3.0f; backing.y -= 2.0f; backing.w += 6.0f; backing.h += 4.0f;
    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 140);
    SDL_RenderFillRect(sdlRenderer, &backing);

    SDL_RenderTexture(sdlRenderer, fpsTexture, nullptr, &dest);
}