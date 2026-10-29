// Nebula.exe starts the game; the first run (and --settings) shows the settings first. NebulaSettings.exe is the
// same program built with NEBULA_SETTINGS_APP and always opens the settings.
#include "launcher_model.h"
#include "ui.h"

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace nebula::launcher {

using namespace ui;
using CS = ControlsSettings;

// Nebula settings: the same choices as the in-game menu (F1, or View+Menu on a controller), saved to the same files.
class SettingsForm : public Form {
public:
    explicit SettingsForm(LaunchSession& session);
    HWND TabsHandle() const { return tabs_; }
    ~SettingsForm() override { EndCapture(); }

private:
    void BuildControlsTab(HWND page);
    void TuningRow(HWND page, const wstring& label, std::unique_ptr<NumberBox>& box, int x, int& y, double min, double max,
                   double step, int decimals);
    void Row(HWND page, const wstring& label, HWND& combo, int& y);
    void LabelAt(HWND page, const wstring& text, int x, int y) { Label(page, text, x, y, 300, 26); }
    int Scheme() const { return std::max(0, ComboIndex(editScheme_)); }
    void LoadTuning();
    void RefreshBindings();
    void StartCapture();
    void EndCapture();
    void FinishCapture(InputSource source);
    void PollCapture();
    bool FilterCapture(MSG& message);
    const MonitorEntry* SelectedMonitor() const;
    void LoadValues();
    void RefreshDisplayChoices();
    void RefreshRefreshChoices();
    void RefreshFrameRateChoices();
    void RefreshScaleChoices();
    void UpscalerChanged();
    void InstallDlss(int index, int previous);
    void DlssDone(int index, int previous, bool installed);
    bool Commit();
    void Play();
    bool ChooseContent();
    void ExportSaves();
    void ImportSaves(bool zip);

    LaunchSession& session_;
    HWND tabs_ = nullptr;
    std::vector<MonitorEntry> monitors_ = EnumerateMonitors();
    GraphicsSettings graphics_ = GraphicsSettings::Load();
    AudioSettings audio_ = AudioSettings::Load();
    GeneralSettings general_ = GeneralSettings::Load();
    ControlsSettings controls_ = ControlsSettings::Load();
    bool loading_ = false;

    HWND monitor_ = nullptr, mode_ = nullptr, resolution_ = nullptr, refresh_ = nullptr, aspect_ = nullptr, internalScale_ = nullptr,
         frameRate_ = nullptr, vsync_ = nullptr;
    HWND antiAliasing_ = nullptr, aaQuality_ = nullptr, upscaler_ = nullptr, dlssQuality_ = nullptr, textureFilter_ = nullptr,
         lighting_ = nullptr, mipmaps_ = nullptr, hudOutput_ = nullptr;
    std::unique_ptr<NumberBox> sharpness_;
    HWND volumes_[6] = {}, volumeValues_[6] = {};
    HWND textSpeed_ = nullptr, language_ = nullptr, recording_ = nullptr, contentLabel_ = nullptr;
    HWND inputSource_ = nullptr, editScheme_ = nullptr, motion_ = nullptr, vibration_ = nullptr, vibrationOn_ = nullptr,
         invertPointer_ = nullptr, gyroPointer_ = nullptr, bindingList_ = nullptr, captureLabel_ = nullptr, importSaves_ = nullptr;
    std::unique_ptr<NumberBox> pointerSpeed_, stickDeadzone_, pointerDeadzone_, shakeSensitivity_, gyroSensitivity_, pointerHide_;

    int captureTarget_ = -1;
    ULONGLONG captureDeadline_ = 0;
    std::unique_ptr<PadCaptureReader> padReader_;
    std::optional<PadCaptureReader::Sample> captureBaseline_;
    bool capturing_ = false;

    std::vector<Size> resolutionChoices_;
    Size windowSize_;
    std::vector<int> frameRateChoices_;
    std::vector<int> upscalerChoices_ = GraphicsSettings::UpscalerChoices();
    int upscalerShown_ = 0;
};

SettingsForm::SettingsForm(LaunchSession& session)
    : Form(
#ifdef NEBULA_SETTINGS_APP
          L"Nebula settings",
#else
          session.SetupComplete ? L"Nebula settings" : L"Welcome to Nebula",
#endif
          760, 560, ui::Font(10)),
      session_(session) {
    bool firstRun = !session.SetupComplete;
    HWND form = Handle();
    Label(form, L"In game: F1, View + Menu (Xbox) or Minus + Plus (Switch) opens the settings.", 12, 10, 736, 40);

    HWND tabs = Tabs(form, 12, 54, 736, 440);
    tabs_ = tabs;
    HWND display = Page(tabs, L"Display");
    HWND image = Page(tabs, L"Graphics");
    HWND input = Page(tabs, L"Controls");
    HWND sound = Page(tabs, L"Audio");
    HWND generalPage = Page(tabs, L"General");
    HWND advanced = Page(tabs, L"Advanced");

    int y = 16;
    Row(display, L"Monitor", monitor_, y);
    Row(display, L"Display mode", mode_, y);
    Row(display, L"Resolution", resolution_, y);
    Row(display, L"Refresh rate", refresh_, y);
    Row(display, L"Aspect ratio", aspect_, y);
    Row(display, L"Internal resolution", internalScale_, y);
    Row(display, L"Frame rate", frameRate_, y);
    vsync_ = Check(display, L"Vertical sync", 12, y, 600, 28);
    for (size_t i = 0; i < monitors_.size(); i++) ComboAdd(monitor_, monitors_[i].Label(int(i)));
    for (const auto& item : GraphicsSettings::kDisplayModes) ComboAdd(mode_, item);
    for (const auto& item : GraphicsSettings::kAspects) ComboAdd(aspect_, item);

    y = 16;
    Row(image, L"Anti-aliasing", antiAliasing_, y);
    Row(image, L"Anti-aliasing quality", aaQuality_, y);
    Row(image, L"Upscaling", upscaler_, y);
    Row(image, L"DLSS quality", dlssQuality_, y);
    LabelAt(image, L"Sharpening (0-100)", 12, y + 3);
    sharpness_ = Number(image, 320, y, 120, 28, 0, 100, 10, 0);
    y += 40;
    Row(image, L"Texture filtering", textureFilter_, y);
    Row(image, L"Lighting", lighting_, y);
    mipmaps_ = Check(image, L"Generated mipmaps", 12, y, 600, 28);
    y += 32;
    hudOutput_ = Check(image, L"HUD at display resolution", 12, y, 600, 28);
    for (const auto& item : GraphicsSettings::kAntiAliasingModes) ComboAdd(antiAliasing_, item);
    for (const auto& item : GraphicsSettings::kAntiAliasingQualities) ComboAdd(aaQuality_, item);
    for (int value : upscalerChoices_) ComboAdd(upscaler_, GraphicsSettings::UpscalerName(value));
    for (const auto& item : GraphicsSettings::kDlssQualities) ComboAdd(dlssQuality_, item);
    for (const auto& item : GraphicsSettings::kTextureFilters) ComboAdd(textureFilter_, item);
    for (const auto& item : GraphicsSettings::kLightingModes) ComboAdd(lighting_, item);

    for (int i = 0; i < 6; i++) {
        int row = 16 + i * 52;
        LabelAt(sound, AudioSettings::kNames[size_t(i)], 12, row + 6);
        volumeValues_[i] = Label(sound, L"", 630, row + 6, 80, 26);
        volumes_[i] = Track(sound, 200, row, 420, 45, 0, 200, 25, 5, 25,
                            [this, i] { SetText(volumeValues_[i], Num(TrackValue(volumes_[i])) + L"%"); });
    }

    BuildControlsTab(input);

    y = 16;
    Row(generalPage, L"Text speed", textSpeed_, y);
    for (const auto& item : GeneralSettings::kTextSpeeds) ComboAdd(textSpeed_, item);
    Row(generalPage, L"Language", language_, y);
    for (const auto& item : GeneralSettings::kLanguages) ComboAdd(language_, item);
    HWND languageNote = Label(generalPage, L"The game starts in this language. While it is running, a change applies the next time it starts.",
                              12, y + 3, 700, 26);
    SetTextColor(languageNote, GetSysColor(COLOR_GRAYTEXT));

    y = 16;
    LabelAt(advanced, L"Game content", 12, y + 3);
    contentLabel_ = Label(advanced, L"", 200, y + 3, 380, 26, SS_PATHELLIPSIS);
    Button(advanced, L"Change…", 590, y, 120, 32, [this] { ChooseContent(); });
    y += 44;
    LabelAt(advanced, L"Recording (next launch)", 12, y + 3);
    recording_ = Combo(advanced, 320, y, 390);
    for (const wchar_t* item : {L"No recording", L"Lightweight recording", L"Detailed recording"}) ComboAdd(recording_, item);
    y += 44;
    Button(advanced, L"Export saves…", 12, y, 170, 32, [this] { ExportSaves(); });
    importSaves_ = Button(advanced, L"Import saves…", 190, y, 170, 32, [this] {
        RECT rect;
        GetWindowRect(importSaves_, &rect);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"From a zip file…");
        AppendMenuW(menu, MF_STRING, 2, L"From a folder…");
        int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rect.left, rect.bottom, 0, Handle(), nullptr);
        DestroyMenu(menu);
        if (choice == 1) ImportSaves(true);
        else if (choice == 2) ImportSaves(false);
    });
    Button(advanced, L"Open save folder", 368, y, 170, 32, [] {
        try {
            CreateDirs(paths::Saves());
            StartProgram(PathCombine(KnownFolder(FOLDERID_Windows), L"explorer.exe"), L"\"" + paths::Saves() + L"\"", L"");
        } catch (const std::exception&) {
        }
    });
    Button(advanced, L"Open diagnostics", 546, y, 170, 32, [] {
        try {
            CreateDirs(paths::Sessions());
            StartProgram(PathCombine(KnownFolder(FOLDERID_Windows), L"explorer.exe"), L"\"" + paths::Sessions() + L"\"", L"");
        } catch (const std::exception&) {
        }
    });
    y += 44;
    Button(advanced, session.Portable ? L"Beta releases" : L"Check for updates", 12, y, 200, 32, [this] {
        try {
            if (session_.Portable) {
                OpenWithShell(L"https://github.com/" + wstring(build::kRepository) + L"/releases");
                return;
            }
            wstring setup = PathCombine(session_.Root, L"Nebula-Setup.exe");
            if (FileExists(setup)) StartProgram(setup, L"--update", session_.Root);
        } catch (const std::exception& error) {
            Message(this, ErrorText(error), L"Nebula", MB_OK | MB_ICONERROR);
        }
    });

    HWND play = Button(form, L"Save and play", 608, 508, 140, 38, [this] { if (Commit()) Play(); });
    SetAccept(play);
    HWND save = Button(form, L"Save", 496, 508, 104, 38, [this] { if (Commit()) Close(); });
#ifndef NEBULA_SETTINGS_APP
    Visible(save, !firstRun);
#else
    (void)firstRun;
    (void)save;
#endif

    LoadValues();
    OnClick(monitor_, [this] {
        if (loading_) return;
        graphics_.FullscreenWidth = graphics_.FullscreenHeight = graphics_.FullscreenRefresh = 0;
        RefreshDisplayChoices();
    });
    OnClick(mode_, [this] { if (!loading_) RefreshDisplayChoices(); });
    OnClick(aspect_, [this] { if (!loading_) RefreshDisplayChoices(); });
    OnClick(resolution_, [this] {
        if (loading_) return;
        int index = ComboIndex(resolution_);
        if (ComboIndex(mode_) == 0 && index >= 0 && index < int(resolutionChoices_.size())) windowSize_ = resolutionChoices_[size_t(index)];
        RefreshRefreshChoices();
    });
    OnClick(refresh_, [this] { if (!loading_) RefreshFrameRateChoices(); });
    // Choosing DLSS before it is downloaded runs Nebula Setup's DLSS download (NVIDIA's license first); closing it
    // without installing goes back to the previous choice.
    upscalerShown_ = ComboIndex(upscaler_);
    OnClick(upscaler_, [this] { UpscalerChanged(); });
    onClosed = [this] { EndCapture(); };
}

void SettingsForm::Row(HWND page, const wstring& label, HWND& combo, int& y) {
    LabelAt(page, label, 12, y + 3);
    combo = Combo(page, 320, y, 390);
    y += 40;
}

void SettingsForm::UpscalerChanged() {
    if (loading_) return;
    int index = ComboIndex(upscaler_);
    if (index >= 0 && upscalerChoices_[size_t(index)] == 3 && !dlss::Installed()) {
        InstallDlss(index, upscalerShown_);
        return;
    }
    upscalerShown_ = index;
    Enable(dlssQuality_, upscalerShown_ >= 0 && upscalerChoices_[size_t(upscalerShown_)] == 3);
    RefreshFrameRateChoices();
}

void SettingsForm::DlssDone(int index, int previous, bool installed) {
    SetEnabled(true);
    loading_ = true;
    if (installed) ComboSetItem(upscaler_, index, GraphicsSettings::UpscalerName(3));
    ComboSelect(upscaler_, installed ? index : previous);
    loading_ = false;
    upscalerShown_ = ComboIndex(upscaler_);
    Enable(dlssQuality_, upscalerShown_ >= 0 && upscalerChoices_[size_t(upscalerShown_)] == 3);
    RefreshFrameRateChoices();
}

// Nebula Setup in the install folder downloads DLSS (Nebula-Setup.exe --install-dlss). Portable copies have none.
void SettingsForm::InstallDlss(int index, int previous) {
    wstring setup = PathCombine(session_.Root, L"Nebula-Setup.exe");
    if (session_.Portable || !FileExists(setup)) {
        Message(this, L"NVIDIA DLSS is installed with Nebula Setup.", L"Nebula", MB_OK | MB_ICONINFORMATION);
        DlssDone(index, previous, false);
        return;
    }
    wstring line = L"\"" + setup + L"\" --install-dlss";
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(setup.c_str(), mutableLine.data(), nullptr, nullptr, FALSE, 0, nullptr, session_.Root.c_str(), &startup, &process)) {
        Message(this, L"Nebula Setup could not start: " + Win32Message(GetLastError()), L"Nebula", MB_OK | MB_ICONERROR);
        DlssDone(index, previous, false);
        return;
    }
    CloseHandle(process.hThread);
    // The window stays responsive (greyed) while Setup's window is open.
    SetEnabled(false);
    HANDLE handle = process.hProcess;
    std::thread([this, handle, index, previous] {
        WaitForSingleObject(handle, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(handle, &code);
        CloseHandle(handle);
        Post([this, code, index, previous] { DlssDone(index, previous, code == 0 && dlss::Installed()); });
    }).detach();
}

// ---- controls ----------------------------------------------------------------------------------------------------

void SettingsForm::TuningRow(HWND page, const wstring& label, std::unique_ptr<NumberBox>& box, int x, int& y, double min, double max,
                             double step, int decimals) {
    Label(page, label, x, y + 3, 170, 24);
    box = Number(page, x + 172, y, 78, 26, min, max, step, decimals);
    y += 27;
}

void SettingsForm::BuildControlsTab(HWND page) {
    int y = 12;
    Label(page, L"Input", 12, y + 3, 180, 24);
    inputSource_ = Combo(page, 200, y, 300);
    for (const wchar_t* item : {L"Auto (keyboard, mouse and controllers)", L"Keyboard and mouse only", L"Controller only"}) ComboAdd(inputSource_, item);
    if (controls_.InputMode == CS::kModeRealWiimote) {
        ComboAdd(inputSource_, L"Wii Remote");
        Enable(inputSource_, false);
    }
    y += 36;
    Label(page, L"Edit controls for", 12, y + 3, 180, 24);
    editScheme_ = Combo(page, 200, y, 300);
    for (const auto& item : CS::kSchemeLabels) ComboAdd(editScheme_, item);
    vibrationOn_ = Check(page, L"Vibration", 516, y + 1, 96, 24);
    vibration_ = Combo(page, 616, y, 100);
    for (const auto& item : CS::kRumbleLabels) ComboAdd(vibration_, item);
    y += 40;

    bindingList_ = List(page, 12, y, 440, 434 - y - 8);
    ListColumns(bindingList_, {{L"Wii input", 170}, {L"Bound to", 226}});
    for (int t = 0; t < CS::kTargetCount; t++) ListAdd(bindingList_, {CS::kTargetLabels[size_t(t)], L""});
    OnDoubleClick(bindingList_, [this] { StartCapture(); });

    int x = 466;
    Button(page, L"Add…", x, y, 80, 32, [this] { StartCapture(); });
    Button(page, L"Clear", x + 84, y, 80, 32, [this] {
        int target = ListSelected(bindingList_);
        if (target < 0) return;
        controls_.Table[Scheme()][size_t(target)] = {};
        RefreshBindings();
    });
    Button(page, L"Defaults", x + 168, y, 80, 32, [this] {
        EndCapture();
        controls_.Table[Scheme()] = CS::DefaultTable(Scheme());
        controls_.Tuning[Scheme()] = CS::DefaultTuning(Scheme());
        LoadTuning();
        RefreshBindings();
    });
    y += 36;
    captureLabel_ = Label(page, L"", x, y, 250, 30);
    y += 30;

    TuningRow(page, L"Pointer speed", pointerSpeed_, x, y, 0.2, 6.0, 0.2, 1);
    TuningRow(page, L"Stick deadzone %", stickDeadzone_, x, y, 0, 50, 2, 0);
    TuningRow(page, L"Pointer stick deadzone %", pointerDeadzone_, x, y, 0, 50, 2, 0);
    TuningRow(page, L"Hide still pointer, s", pointerHide_, x, y, 0, 30, 1, 0);
    invertPointer_ = Check(page, L"Invert pointer Y", x, y, 250, 24);
    y += 26;
    Label(page, L"Motion", x, y + 3, 88, 24);
    motion_ = Combo(page, x + 92, y, 158);
    for (const auto& item : CS::kMotionLabels) ComboAdd(motion_, item);
    y += 27;
    TuningRow(page, L"Shake sensitivity", shakeSensitivity_, x, y, 1, 5, 1, 0);
    gyroPointer_ = Check(page, L"Gyro pointer (right stick: camera)", x, y, 250, 24);
    y += 26;
    TuningRow(page, L"Gyro sensitivity", gyroSensitivity_, x, y, 0.2, 5.0, 0.1, 1);

    pointerSpeed_->changed = [this] { if (!loading_) controls_.Tuning[Scheme()].PointerSpeed = float(pointerSpeed_->value); };
    stickDeadzone_->changed = [this] { if (!loading_) controls_.Tuning[Scheme()].StickDeadzone = float(stickDeadzone_->value) / 100.0f; };
    pointerDeadzone_->changed = [this] { if (!loading_) controls_.Tuning[Scheme()].PointerDeadzone = float(pointerDeadzone_->value) / 100.0f; };
    shakeSensitivity_->changed = [this] { if (!loading_) controls_.Tuning[Scheme()].ShakeSensitivity = int(shakeSensitivity_->value); };
    OnClick(invertPointer_, [this] { if (!loading_) controls_.Tuning[Scheme()].InvertPointerY = Checked(invertPointer_); });
    OnClick(gyroPointer_, [this] { if (!loading_) controls_.Tuning[Scheme()].GyroPointer = Checked(gyroPointer_); });
    gyroSensitivity_->changed = [this] { if (!loading_) controls_.Tuning[Scheme()].GyroSensitivity = float(gyroSensitivity_->value); };
    pointerHide_->changed = [this] { if (!loading_) controls_.Tuning[Scheme()].PointerHideSeconds = float(pointerHide_->value); };
    OnClick(motion_, [this] { if (!loading_) controls_.Tuning[Scheme()].Motion = std::max(0, ComboIndex(motion_)); });
    OnClick(vibration_, [this] {
        if (!loading_) controls_.Tuning[Scheme()].Rumble = CS::kRumbleSteps[size_t(std::max(0, ComboIndex(vibration_)))];
    });
    OnClick(vibrationOn_, [this] {
        if (!loading_) controls_.Tuning[Scheme()].Vibration = Checked(vibrationOn_);
        Enable(vibration_, IsWindowEnabled(vibrationOn_) && Checked(vibrationOn_));
    });
    OnClick(editScheme_, [this] { EndCapture(); LoadTuning(); RefreshBindings(); });

    ComboSelect(inputSource_, std::min(controls_.InputMode, ComboCount(inputSource_) - 1));
    ComboSelect(editScheme_, controls_.InputMode == CS::kModeController ? CS::kXbox : CS::kKeyboard);
}

void SettingsForm::LoadTuning() {
    loading_ = true;
    const SchemeTuning& t = controls_.Tuning[Scheme()];
    bool pad = Scheme() != CS::kKeyboard, imu = CS::HasMotion(Scheme());
    pointerSpeed_->Set(t.PointerSpeed);
    stickDeadzone_->Set(double(t.StickDeadzone) * 100.0);
    pointerDeadzone_->Set(double(t.PointerDeadzone) * 100.0);
    SetChecked(invertPointer_, t.InvertPointerY);
    ComboSelect(motion_, std::max(0, std::min(2, t.Motion)));
    shakeSensitivity_->Set(t.ShakeSensitivity);
    SetChecked(gyroPointer_, t.GyroPointer);
    gyroSensitivity_->Set(t.GyroSensitivity);
    pointerHide_->Set(t.PointerHideSeconds);
    int rumble = CS::SnapRumble(float(t.Rumble));
    auto step = std::find(CS::kRumbleSteps.begin(), CS::kRumbleSteps.end(), rumble);
    ComboSelect(vibration_, std::max(0, int(step - CS::kRumbleSteps.begin())));
    SetChecked(vibrationOn_, t.Vibration);
    stickDeadzone_->Enable(pad);
    pointerDeadzone_->Enable(pad);
    pointerHide_->Enable(pad);
    Enable(vibrationOn_, pad);
    Enable(vibration_, pad && t.Vibration);
    Enable(motion_, imu);
    shakeSensitivity_->Enable(imu);
    Enable(gyroPointer_, imu);
    gyroSensitivity_->Enable(imu);
    loading_ = false;
}

void SettingsForm::RefreshBindings() {
    for (int t = 0; t < CS::kTargetCount; t++)
        ListSetCell(bindingList_, t, 1,
                    captureTarget_ == t ? L"Press an input…" : CS::FormatBindings(Scheme(), controls_.Table[Scheme()][size_t(t)]));
}

void SettingsForm::StartCapture() {
    int target = ListSelected(bindingList_);
    if (target < 0) {
        SetText(captureLabel_, L"Select a Wii input first.");
        return;
    }
    EndCapture();
    captureTarget_ = target;
    captureDeadline_ = GetTickCount64() + 8000;
    captureBaseline_.reset();
    if (Scheme() == CS::kKeyboard) {
        SetText(captureLabel_, L"Press a key or mouse button. Esc cancels.");
    } else {
        SetText(captureLabel_, L"Press a button, push a stick or pull a trigger. Esc cancels.");
        padReader_ = std::make_unique<PadCaptureReader>(Scheme() != CS::kXbox);
    }
    capturing_ = true;
    SetMessageFilter([this](MSG& message) { return FilterCapture(message); });
    StartTimer(1, 15, [this] { PollCapture(); });
    RefreshBindings();
}

void SettingsForm::EndCapture() {
    if (Handle()) StopTimer(1);
    if (capturing_) {
        SetMessageFilter(nullptr);
        capturing_ = false;
    }
    padReader_.reset();
    if (captureTarget_ >= 0) {
        captureTarget_ = -1;
        if (Handle()) RefreshBindings();
    }
}

void SettingsForm::FinishCapture(InputSource source) {
    int target = captureTarget_;
    int scheme = Scheme();
    EndCapture();
    if (target < 0) return;
    if (!CS::Allowed(scheme, target, source)) {
        SetText(captureLabel_, CS::FormatSource(scheme, source) + L" cannot drive " + CS::kTargetLabels[size_t(target)] + L".");
        return;
    }
    controls_.Table[scheme][size_t(target)] = CS::AddBinding(controls_.Table[scheme][size_t(target)], source);
    SetText(captureLabel_, L"");
    RefreshBindings();
}

void SettingsForm::PollCapture() {
    if (captureTarget_ < 0) return;
    if (GetTickCount64() > captureDeadline_) {
        EndCapture();
        SetText(captureLabel_, L"");
        return;
    }
    if (!padReader_) return;
    auto sample = padReader_->Read(Scheme());
    if (!sample.Connected) {
        SetText(captureLabel_, L"No " + CS::kSchemeLabels[size_t(Scheme())] + L" found. Esc cancels.");
        return;
    }
    if (!captureBaseline_) {
        captureBaseline_ = sample;
        return;
    }
    InputSource source;
    if (PadCaptureReader::Detect(*captureBaseline_, sample, source)) FinishCapture(source);
    else captureBaseline_ = sample;
}

// Window messages while a binding waits for input: keys and mouse buttons bind (keyboard scheme); Esc cancels.
bool SettingsForm::FilterCapture(MSG& m) {
    UINT msg = m.message;
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) return true;
    bool keyboardScheme = Scheme() == CS::kKeyboard;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        int vk = int(m.wParam & 0xFF);
        LPARAM lp = m.lParam;
        if (vk == 0x1B) {
            EndCapture();
            SetText(captureLabel_, L"");
            return true;
        }
        if (!keyboardScheme) return true;
        if (vk == 0x10) vk = MapVirtualKeyW(UINT((lp >> 16) & 0xFF), MAPVK_VSC_TO_VK_EX) == 0xA1 ? 0xA1 : 0xA0;
        else if (vk == 0x11) vk = (lp & (1 << 24)) ? 0xA3 : 0xA2;
        FinishCapture({SourceKind::Key, vk});
        return true;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) {
        if (!keyboardScheme) return false;
        int button = msg == WM_LBUTTONDOWN ? 0 : msg == WM_RBUTTONDOWN ? 1 : msg == WM_MBUTTONDOWN ? 2 : (HIWORD(m.wParam) == 1 ? 3 : 4);
        FinishCapture({SourceKind::Mouse, button});
        return true;
    }
    return false;
}

// ---- display -----------------------------------------------------------------------------------------------------

const MonitorEntry* SettingsForm::SelectedMonitor() const {
    int index = ComboIndex(monitor_);
    if (index >= 0 && index < int(monitors_.size())) return &monitors_[size_t(index)];
    return FindMonitor(monitors_, L"");
}

void SettingsForm::LoadValues() {
    loading_ = true;
    const MonitorEntry* current = FindMonitor(monitors_, graphics_.Monitor);
    ComboSelect(monitor_, current ? int(current - monitors_.data()) : 0);
    ComboSelect(mode_, std::max(0, std::min(graphics_.DisplayMode, int(GraphicsSettings::kDisplayModes.size()) - 1)));
    ComboSelect(aspect_, std::max(0, std::min(graphics_.Aspect, int(GraphicsSettings::kAspects.size()) - 1)));
    SetChecked(vsync_, graphics_.VSync);
    ComboSelect(antiAliasing_, graphics_.AntiAliasing);
    ComboSelect(aaQuality_, graphics_.Quality);
    auto found = std::find(upscalerChoices_.begin(), upscalerChoices_.end(), graphics_.Upscaler);
    if (found == upscalerChoices_.end()) found = std::find(upscalerChoices_.begin(), upscalerChoices_.end(), 1);
    ComboSelect(upscaler_, int(found - upscalerChoices_.begin()));
    ComboSelect(dlssQuality_, std::max(0, std::min(graphics_.TemporalQuality, int(GraphicsSettings::kDlssQualities.size()) - 1)));
    Enable(dlssQuality_, upscalerChoices_[size_t(ComboIndex(upscaler_))] == 3);
    sharpness_->Set(graphics_.Sharpness);
    ComboSelect(textureFilter_, graphics_.TextureFilter);
    ComboSelect(lighting_, graphics_.PerPixelLighting ? 0 : 1);
    SetChecked(mipmaps_, graphics_.GeneratedMipmaps);
    SetChecked(hudOutput_, graphics_.HudOutputResolution);
    for (int i = 0; i < 6; i++) {
        SetTrackValue(volumes_[i], std::max(0, std::min(200, audio_.Percent[i])));
        SetText(volumeValues_[i], Num(TrackValue(volumes_[i])) + L"%");
    }
    ComboSelect(textSpeed_, std::max(0, std::min(general_.TextSpeed - 1, int(GeneralSettings::kTextSpeeds.size()) - 1)));
    ComboSelect(language_, std::max(0, std::min(general_.Language, int(GeneralSettings::kLanguages.size()) - 1)));
    ComboSelect(recording_, int(session_.Recording));
    SetText(contentLabel_, session_.Content);
    windowSize_ = {graphics_.WindowWidth, graphics_.WindowHeight};
    loading_ = false;
    LoadTuning();
    RefreshBindings();
    RefreshDisplayChoices();
}

// Resolution: the desktop (borderless), or window sizes that fit the monitor's work area. With a fixed aspect the
// window sizes have that shape, and the selection moves to the smallest one at least as tall as the current window.
void SettingsForm::RefreshDisplayChoices() {
    loading_ = true;
    const MonitorEntry* screen = SelectedMonitor();
    ComboClear(resolution_);
    resolutionChoices_.clear();
    int selected = 0;
    int screenWidth = screen ? screen->Width : 1920, screenHeight = screen ? screen->Height : 1080;
    if (ComboIndex(mode_) == 1) {
        resolutionChoices_.push_back({screenWidth, screenHeight});
        ComboAdd(resolution_, Num(screenWidth) + L" × " + Num(screenHeight) + L" (desktop)");
    } else {
        RECT work = screen ? screen->WorkArea : RECT{0, 0, 1920, 1040};
        int frameW = GetSystemMetrics(SM_CXFRAME), frameH = GetSystemMetrics(SM_CYFRAME);
        int maxW = (work.right - work.left) - 2 * frameW - 16;
        int maxH = (work.bottom - work.top) - GetSystemMetrics(SM_CYCAPTION) - 2 * frameH - 16;
        int aspect = ComboIndex(aspect_);
        if (aspect > 0) {
            resolutionChoices_ = GraphicsSettings::AspectWindowSizes(aspect, maxW, maxH);
            if (resolutionChoices_.empty()) {
                auto all = GraphicsSettings::AspectWindowSizes(aspect, INT_MAX, INT_MAX);
                if (!all.empty()) resolutionChoices_.push_back(all[0]);
            }
            selected = GraphicsSettings::SnapWindowSize(resolutionChoices_, windowSize_);
        } else {
            std::vector<Size> sizes = {{854, 480},   {1024, 768},  {1280, 720},  {1280, 800},  {1366, 768},  {1440, 900},  {1600, 900},
                                       {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440},
                                       {3840, 1080}, {3840, 2160}, {5120, 1440}, {5120, 2160}, windowSize_};
            if (screen)
                for (const auto& s : screen->Resolutions()) sizes.push_back(s);
            std::vector<Size> distinct;
            for (const auto& s : sizes)
                if (std::find(distinct.begin(), distinct.end(), s) == distinct.end() && s.w <= maxW && s.h <= maxH) distinct.push_back(s);
            std::stable_sort(distinct.begin(), distinct.end(), [](const Size& a, const Size& b) { return (long long)a.w * a.h < (long long)b.w * b.h; });
            resolutionChoices_ = distinct;
            if (resolutionChoices_.empty()) resolutionChoices_.push_back({854, 480});
            long long want = (long long)windowSize_.w * windowSize_.h;
            for (size_t i = 0; i < resolutionChoices_.size(); i++)
                if ((long long)resolutionChoices_[i].w * resolutionChoices_[i].h <= want) selected = int(i);
            auto exact = std::find(resolutionChoices_.begin(), resolutionChoices_.end(), windowSize_);
            if (exact != resolutionChoices_.end()) selected = int(exact - resolutionChoices_.begin());
        }
        for (const auto& s : resolutionChoices_) ComboAdd(resolution_, Num(s.w) + L" × " + Num(s.h));
        windowSize_ = resolutionChoices_[size_t(selected)];
    }
    ComboSelect(resolution_, std::min(selected, ComboCount(resolution_) - 1));
    loading_ = false;
    RefreshRefreshChoices();
}

void SettingsForm::RefreshRefreshChoices() {
    loading_ = true;
    const MonitorEntry* screen = SelectedMonitor();
    ComboClear(refresh_);
    ComboAdd(refresh_, L"Desktop (" + Num(screen ? screen->Refresh : 60) + L" Hz)");
    ComboSelect(refresh_, 0);
    Enable(refresh_, false);
    Enable(resolution_, ComboIndex(mode_) != 1);
    RefreshScaleChoices();
    RefreshFrameRateChoices();
    loading_ = false;
}

void SettingsForm::RefreshFrameRateChoices() {
    int index = ComboIndex(frameRate_);
    int want = index >= 0 && index < int(frameRateChoices_.size()) ? frameRateChoices_[size_t(index)] : graphics_.FrameRate;
    const MonitorEntry* screen = SelectedMonitor();
    frameRateChoices_ = GraphicsSettings::FrameRates(screen ? screen->Refresh : 60);
    // DLSS upscales interpolated frames at multiples of 60.
    int up = ComboIndex(upscaler_);
    bool dlssOn = up >= 0 && up < int(upscalerChoices_.size()) && upscalerChoices_[size_t(up)] == 3;
    if (dlssOn)
        frameRateChoices_.erase(std::remove_if(frameRateChoices_.begin(), frameRateChoices_.end(),
                                               [](int rate) { return rate == 0 || (rate > 60 && rate % 60 != 0); }),
                                frameRateChoices_.end());
    ComboClear(frameRate_);
    int selected = int(frameRateChoices_.size()) - 1;
    for (size_t i = 0; i < frameRateChoices_.size(); i++) {
        int rate = frameRateChoices_[i];
        ComboAdd(frameRate_, rate == 0 ? L"Unlimited" : Num(rate) + L" FPS");
        if (want != 0 && rate != 0 && rate <= want) selected = int(i);
    }
    ComboSelect(frameRate_, selected);
}

// Internal resolution: matches the output, or 1x up to the scale that fills the selected display.
void SettingsForm::RefreshScaleChoices() {
    const MonitorEntry* screen = SelectedMonitor();
    int cap = ScaleCovering(screen ? screen->Width : 0, screen ? screen->Height : 0);
    int previous = ComboIndex(internalScale_);
    bool hadItems = ComboCount(internalScale_) > 0;
    ComboClear(internalScale_);
    ComboAdd(internalScale_, L"Match output");
    for (int s = 1; s <= cap; s++) ComboAdd(internalScale_, Num(s) + L"x (" + Num(640 * s) + L" × " + Num(528 * s) + L")");
    int want = hadItems ? previous : (graphics_.MatchOutput ? 0 : std::min(graphics_.EfbScale, cap));
    ComboSelect(internalScale_, std::max(0, std::min(want, ComboCount(internalScale_) - 1)));
}

// ---- saving and playing ------------------------------------------------------------------------------------------

bool SettingsForm::Commit() {
    try {
        const MonitorEntry* screen = SelectedMonitor();
        graphics_.Monitor = screen ? screen->Device : L"";
        graphics_.DisplayMode = std::max(0, ComboIndex(mode_));
        int resolution = ComboIndex(resolution_);
        if (resolution >= 0 && resolution < int(resolutionChoices_.size()) && graphics_.DisplayMode == 0) {
            graphics_.WindowWidth = resolutionChoices_[size_t(resolution)].w;
            graphics_.WindowHeight = resolutionChoices_[size_t(resolution)].h;
        }
        graphics_.Aspect = std::max(0, ComboIndex(aspect_));
        graphics_.MatchOutput = ComboIndex(internalScale_) <= 0;
        if (!graphics_.MatchOutput) graphics_.EfbScale = ComboIndex(internalScale_);
        graphics_.VSync = Checked(vsync_);
        int rate = ComboIndex(frameRate_);
        if (rate >= 0 && rate < int(frameRateChoices_.size())) graphics_.FrameRate = frameRateChoices_[size_t(rate)];
        graphics_.AntiAliasing = ComboIndex(antiAliasing_);
        graphics_.Quality = ComboIndex(aaQuality_);
        int up = ComboIndex(upscaler_);
        if (up >= 0 && up < int(upscalerChoices_.size())) graphics_.Upscaler = upscalerChoices_[size_t(up)];
        graphics_.TemporalQuality = std::max(0, ComboIndex(dlssQuality_));
        graphics_.Sharpness = int(sharpness_->Get());
        graphics_.TextureFilter = ComboIndex(textureFilter_);
        graphics_.PerPixelLighting = ComboIndex(lighting_) == 0;
        graphics_.GeneratedMipmaps = Checked(mipmaps_);
        graphics_.HudOutputResolution = Checked(hudOutput_);
        // Typed tuning values not yet confirmed by leaving their box.
        for (auto* box : {pointerSpeed_.get(), stickDeadzone_.get(), pointerDeadzone_.get(), shakeSensitivity_.get(),
                          gyroSensitivity_.get(), pointerHide_.get()})
            box->Get();
        graphics_.Save();
        for (int i = 0; i < 6; i++) audio_.Percent[i] = TrackValue(volumes_[i]);
        audio_.Save();
        general_.TextSpeed = std::max(0, ComboIndex(textSpeed_)) + 1;
        general_.Language = std::max(0, ComboIndex(language_));
        general_.Save();
        EndCapture();
        if (controls_.InputMode != CS::kModeRealWiimote) controls_.InputMode = std::max(0, ComboIndex(inputSource_));
        controls_.Save();
        session_.Recording = RecordingMode(std::max(0, ComboIndex(recording_)));
        session_.Save();
        return true;
    } catch (const std::exception& error) {
        Message(this, L"Settings could not be saved: " + ErrorText(error), L"Nebula", MB_OK | MB_ICONERROR);
        return false;
    }
}

void SettingsForm::Play() {
    if (!FileExists(PathCombine(session_.Content, L"game.pak")) && !ChooseContent()) return;
    auto error = session_.TryLaunch(Handle());
    if (error) {
        Message(this, *error, L"Nebula", MB_OK | MB_ICONERROR);
        return;
    }
    // A recording keeps this process alive, through its thread, until the game exits.
    Close();
}

bool SettingsForm::ChooseContent() {
    auto file = OpenFileDialog(this, L"Choose game.pak from your Nebula installation", {{L"Nebula game content", L"game.pak"}});
    if (!file) return false;
    session_.Content = DirName(*file);
    SetText(contentLabel_, session_.Content);
    return true;
}

void SettingsForm::ExportSaves() {
    auto folder = FolderDialog(this, L"Choose a folder for the saves archive.");
    if (!folder) return;
    try {
        wstring archive = saves::Export(*folder);
        Message(this, L"Saves exported to " + archive + L".", L"Nebula", MB_OK | MB_ICONINFORMATION);
    } catch (const std::exception& error) {
        Message(this, ErrorText(error), L"Nebula", MB_OK | MB_ICONERROR);
    }
}

// A zip from Export saves (or another zip of a saves folder), or a saves folder.
void SettingsForm::ImportSaves(bool zip) {
    std::optional<wstring> source;
    if (zip) source = OpenFileDialog(this, L"Import saves", {{L"Zip archives (*.zip)", L"*.zip"}});
    else
        source = FolderDialog(this, L"Choose a Nebula saves folder, or Dolphin's save folder for Super Mario Galaxy (the folder that holds "
                                    L"GameData.bin; in Dolphin, right-click the game and choose Open Wii Save Folder).");
    if (!source) return;
    try {
        auto message = saves::Import(*source, [this] {
            return Message(this, L"Replace the current saves? They are moved to a backup folder.", L"Nebula", MB_YESNO | MB_ICONQUESTION) == IDYES;
        });
        if (message) Message(this, *message, L"Nebula", MB_OK | MB_ICONINFORMATION);
    } catch (const std::exception& error) {
        Message(this, ErrorText(error), L"Nebula", MB_OK | MB_ICONERROR);
    }
}

static bool OpensSettings(const std::vector<wstring>& args, const LaunchSession& session) {
#ifdef NEBULA_SETTINGS_APP
    (void)args;
    (void)session;
    return true;
#else
    for (const auto& arg : args)
        if (IEquals(arg, L"--settings")) return true;
    return !session.SetupComplete;
#endif
}

static int Main() {
    ui::Init();
    try {
        LaunchSession session;
        bool settings = OpensSettings(CommandLineArgs(), session);
        if (!settings) {
            auto error = session.TryLaunch(nullptr);
            if (!error) {
                if (session.RecordingThread) WaitForSingleObject(session.RecordingThread, INFINITE);
                return 0;
            }
            MessageBoxW(nullptr, error->c_str(), L"Nebula", MB_OK | MB_ICONERROR);
        }
#ifdef NEBULA_UI_SNAPSHOTS
        if (auto directory = ui::SnapshotDirectory()) {
            SettingsForm form(session);
            form.Show();
            ui::Pump(400);
            for (int tab = 0; tab < 6; tab++) {
                form.SelectTab(form.TabsHandle(), tab);
                ui::Pump(250);
                ui::SaveSnapshot(form.Handle(), PathCombine(*directory, L"settings-tab" + Num(tab) + L".bmp"));
            }
            return 0;
        }
#endif
        {
            SettingsForm form(session);
            ui::Run(form);
        }
        if (session.RecordingThread) WaitForSingleObject(session.RecordingThread, INFINITE);
        return 0;
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, ErrorText(error).c_str(), L"Nebula", MB_OK | MB_ICONERROR);
        return 1;
    }
}

}  // namespace nebula::launcher

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return nebula::launcher::Main(); }
