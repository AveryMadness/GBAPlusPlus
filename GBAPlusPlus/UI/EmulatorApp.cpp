// EmulatorApp.cpp
#include "EmulatorApp.h"
#include "RegisterFrame.h"
#include "MemoryViewerFrame.h"
#include <fstream>
#include <vector>

enum {
    ID_LoadBIOS = wxID_HIGHEST + 1,
    ID_Step,
    ID_Run,
    ID_Pause,
    ID_Reset,
    ID_ShowRegisters,
    ID_ShowMemory,
    ID_FrameTimer
};

wxBEGIN_EVENT_TABLE(EmulatorFrame, wxFrame)
    EVT_MENU(wxID_OPEN, EmulatorFrame::OnOpen)
    EVT_MENU(ID_LoadBIOS, EmulatorFrame::OnLoadBIOS)
    EVT_MENU(wxID_EXIT, EmulatorFrame::OnExit)
    EVT_MENU(ID_Step, EmulatorFrame::OnStep)
    EVT_MENU(ID_Run, EmulatorFrame::OnRun)
    EVT_MENU(ID_Pause, EmulatorFrame::OnPause)
    EVT_MENU(ID_Reset, EmulatorFrame::OnReset)
    EVT_MENU(ID_ShowRegisters, EmulatorFrame::OnShowRegisters)
    EVT_MENU(ID_ShowMemory, EmulatorFrame::OnShowMemory)
    EVT_TIMER(ID_FrameTimer, EmulatorFrame::OnTimer)
    EVT_IDLE(EmulatorFrame::OnIdle)
wxEND_EVENT_TABLE()

bool EmulatorApp::OnInit() {
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
    , accumulator(0.0)
{
    // Menu bar
    wxMenuBar* menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append(ID_LoadBIOS, "Load &BIOS...\tCtrl-B", "Load GBA BIOS file");
    fileMenu->Append(wxID_OPEN, "&Open ROM...\tCtrl-O", "Load GBA ROM file");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit\tAlt-F4", "Exit the emulator");
    menuBar->Append(fileMenu, "&File");

    wxMenu* emuMenu = new wxMenu();
    emuMenu->Append(ID_Step,  "&Step\tSpace",   "Execute one instruction");
    emuMenu->Append(ID_Run,   "&Run\tF5",        "Run emulation");
    emuMenu->Append(ID_Pause, "&Pause\tF6",      "Pause emulation");
    emuMenu->AppendSeparator();
    emuMenu->Append(ID_Reset, "R&eset\tCtrl-R", "Reset emulator");
    menuBar->Append(emuMenu, "&Emulation");

    wxMenu* debugMenu = new wxMenu();
    debugMenu->Append(ID_ShowRegisters, "Show &Registers\tCtrl-Shift-R", "Show register window");
    debugMenu->Append(ID_ShowMemory,    "Show &Memory\tCtrl-Shift-M",    "Show memory viewer");
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

    frameTimer = new wxTimer(this, ID_FrameTimer);

    InitializeEmulator();

    lastFrameTime = std::chrono::high_resolution_clock::now();
}

EmulatorFrame::~EmulatorFrame() {
    if (frameTimer->IsRunning())
        frameTimer->Stop();
    delete frameTimer;
    
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

    wxLogMessage("Emulator initialized");
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
        memoryBus->loadBIOS(buffer.data(), size);
        biosLoaded = true;
        SetStatusText("BIOS loaded: " + path, 0);
        wxLogMessage("BIOS loaded successfully (%d bytes)", (int)size);

        if (romLoaded) {
            cpu->InitializeCpuForExecution();
            SetStatusText("Ready to run", 0);
        }
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
        memoryBus->loadROM(buffer.data(), size);
        romLoaded = true;
        SetStatusText("ROM loaded: " + path, 0);
        wxLogMessage("ROM loaded successfully (%d bytes)", (int)size);

        if (biosLoaded) {
            cpu->InitializeCpuForExecution();
            SetStatusText("Ready to run", 0);
        } else {
            SetStatusText("Load BIOS to continue", 0);
        }
    } else {
        wxMessageBox("Failed to read ROM file", "Error", wxICON_ERROR);
    }
}

void EmulatorFrame::OnExit(wxCommandEvent& event) {
    Close(true);
}

void EmulatorFrame::OnStep(wxCommandEvent& event) {
    if (!biosLoaded || !romLoaded) {
        wxMessageBox("Please load BIOS and ROM first", "Not Ready", wxICON_WARNING);
        return;
    }
    if (cpu) {
        try
        {
            cpu->runCpuStep();
        }
        catch (const std::exception& e)
        {
            isRunning = false;
            frameTimer->Stop();
            wxMessageBox(e.what(), "CPU Error", wxICON_ERROR);
        }
        UpdateDebugWindows();
        SetStatusText(wxString::Format("PC: 0x%08X",
            *registers->GetRegister(PROGRAM_COUNTER)), 1);
    }
}

void EmulatorFrame::OnRun(wxCommandEvent& event) {
    if (!biosLoaded || !romLoaded) {
        wxMessageBox("Please load BIOS and ROM first", "Not Ready", wxICON_WARNING);
        return;
    }
    isRunning = true;
    isPaused  = false;
    frameTimer->Start(16);
    SetStatusText("Running", 1);
    wxLogMessage("Emulation started");
}

void EmulatorFrame::OnPause(wxCommandEvent& event) {
    isRunning = false;
    isPaused  = true;
    frameTimer->Stop();
    SetStatusText("Paused", 1);
    UpdateDebugWindows();
    wxLogMessage("Emulation paused");
}

void EmulatorFrame::OnReset(wxCommandEvent& event) {
    isRunning = false;
    isPaused  = false;
    frameTimer->Stop();

    if (biosLoaded && romLoaded) {
        cpu->InitializeCpuForExecution();
        accumulator   = 0.0;
        lastFrameTime = std::chrono::high_resolution_clock::now();
        SetStatusText("Reset complete - Ready to run", 0);
        SetStatusText("Stopped", 1);
        UpdateDebugWindows();
        wxLogMessage("Emulator reset");
    }
}

void EmulatorFrame::OnShowRegisters(wxCommandEvent& event) {
    // Create lazily; window hides itself on close (Veto) rather than destroying,
    // so we only ever create it once. Check IsShown to avoid creating duplicates
    // after it's been hidden.
    if (!registerWindow) {
        registerWindow = new RegisterFrame(this, registers);
    }
    registerWindow->Show();
    registerWindow->Raise();
}

void EmulatorFrame::OnShowMemory(wxCommandEvent& event) {
    if (!memoryWindow) {
        memoryWindow = new MemoryViewerFrame(this, memoryBus);
    }
    memoryWindow->Show();
    memoryWindow->Raise();
}

void EmulatorFrame::OnTimer(wxTimerEvent& event) {
    if (!isRunning || !cpu) return;

    auto   currentTime = std::chrono::high_resolution_clock::now();
    double deltaTime   = std::chrono::duration<double>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    // Clamp delta to avoid spiral of death after pauses/debugging
    if (deltaTime > 0.1) deltaTime = 0.1;

    accumulator += deltaTime;

    while (accumulator >= FRAME_TIME) {
        for (int i = 0; i < CYCLES_PER_FRAME; i++)
        {
            try
            {
                cpu->runCpuStep();
            }
            catch (const std::exception& e)
            {
                isRunning = false;
                frameTimer->Stop();
                wxMessageBox(e.what(), "CPU Error", wxICON_ERROR);
            }
        }
        accumulator -= FRAME_TIME;
    }

    sdlPanel->Render();

    static int frameCount = 0;
    if (++frameCount >= 10) {
        frameCount = 0;
        UpdateDebugWindows();
        SetStatusText(wxString::Format("PC: 0x%08X",
            *registers->GetRegister(PROGRAM_COUNTER)), 1);
    }
}

void EmulatorFrame::OnIdle(wxIdleEvent& event) {
    if (isRunning)
        event.RequestMore();
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
    if (sdlRenderer) SDL_DestroyRenderer(sdlRenderer);
    if (sdlWindow)   SDL_DestroyWindow(sdlWindow);
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

    wxLogMessage("SDL initialized successfully");
}

void SDLPanel::Render() {
    if (!sdlRenderer) return;

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);

    // TODO: Render GBA screen here
    SDL_SetRenderDrawColor(sdlRenderer, 50, 50, 100, 255);
    SDL_FRect rect = {50, 50, 200, 100};
    SDL_RenderFillRect(sdlRenderer, &rect);

    SDL_RenderPresent(sdlRenderer);
}