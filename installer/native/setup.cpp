// Nebula-Setup.exe [--update | --apply-update | --uninstall | --rollback] [--input PATH] [--root PATH] [--source ZIP|DIR]
//   [--jobs N] [--desktop-shortcut] [--unattended] [--remove-user-data] [--accept-build-tools-license]
// Nebula-Setup.exe --finish-uninstall PID --root PATH (the temporary copy that removes Setup after an uninstall)
// Nebula-Setup.exe --install-dlss (the launcher's DLSS choice: NVIDIA's license and the download; exit code 0 once
//   DLSS is installed)
#include "setup_engine.h"
#include "ui.h"

#include <shlobj.h>

#include <thread>

namespace nebula::setup {

using namespace ui;
using Options = std::map<wstring, wstring>;  // keys lower-cased

static bool Has(const Options& options, const wstring& key) { return options.count(key) != 0; }
static const COLORREF kFirebrick = RGB(178, 34, 34);

// ---- NVIDIA's DLSS license and the download (with licenseOnly: the license and a Close button) ------------------

class DlssInstallDialog : public Form {
public:
    DlssInstallDialog(bool licenseOnly, Form* owner)
        : Form(licenseOnly ? L"NVIDIA RTX SDKs license" : L"NVIDIA DLSS", 640, 540, ui::Font(9.5), owner, false), licenseOnly_(licenseOnly) {
        HWND form = Handle();
        int y = 14;
        if (!licenseOnly) {
            Label(form, L"Nebula downloads DLSS from NVIDIA's Streamline 2.14.1 release on GitHub (" + FormatBytes(dlssdownload::kDownloadBytes) +
                            L"). The game uses it the next time it starts.",
                  16, y, 608, 42);
            y += 48;
        }
        int height = licenseOnly ? 470 : 350;
        license_ = Edit(form, L"Loading the license from GitHub…", 16, y, 608, height, true, true);
        SetWhiteBackground(license_);
        SendMessageW(license_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::Font(9)), TRUE);
        y += height + 10;
        if (!licenseOnly) {
            accept_ = Check(form, L"I accept the NVIDIA RTX SDKs license (required)", 16, y, 608, 24, [this] {
                ResetTextColor(accept_);
                SetText(status_, L"");
            });
            Enable(accept_, false);
            y += 28;
            status_ = Label(form, L"", 16, y, 608, 22);
            y += 24;
            bar_ = Progress(form, 16, y, 608, 14);
            Visible(bar_, false);
            install_ = Button(form, L"Install", 418, 496, 100, 30, [this] { OnInstall(); });
            Enable(install_, false);
            SetAccept(install_);
        }
        close_ = Button(form, licenseOnly ? L"Close" : L"Cancel", 524, 496, 100, 30, [this] { Close(); });
        SetCancel(close_);
        onShown = [this] { LoadLicense(); };
        onClosing = [this] {
            cancel_.Cancel();
            if (working_) {
                // The download stops at its next read; the window closes then.
                Enable(close_, false);
                SetText(status_, L"Canceling…");
                return false;
            }
            return true;
        };
    }

private:
    void LoadLicense() {
        std::thread([this] {
            std::optional<wstring> text, error;
            try {
                text = dlssdownload::LicenseText(cancel_);
            } catch (const std::exception& failure) {
                if (IsKind(failure, ErrorKind::Canceled)) return;
                error = ErrorText(failure);
            }
            Post([this, text, error] {
                if (!text) {
                    SetText(license_, L"The license could not be loaded: " + error.value_or(L"") + L"\r\n\r\nCheck the internet connection and try again.");
                    return;
                }
                SetText(license_, Replace(Replace(*text, L"\r\n", L"\n"), L"\n", L"\r\n"));
                if (!licenseOnly_) {
                    Enable(accept_, true);
                    Enable(install_, true);
                }
            });
        }).detach();
    }

    void OnInstall() {
        if (!Checked(accept_)) {
            SetTextColor(accept_, kFirebrick);
            SetTextColor(status_, kFirebrick);
            SetText(status_, L"Accept the license to install DLSS.");
            SetFocus(accept_);
            return;
        }
        Enable(install_, false);
        Enable(accept_, false);
        Visible(bar_, true);
        ResetTextColor(status_);
        SetText(status_, L"Downloading…");
        working_ = true;
        std::thread([this] {
            std::optional<wstring> failure;
            bool canceled = false;
            try {
                dlssdownload::Install([this](long long done, long long total) {
                    Post([this, done, total] {
                        SetProgress(bar_, total > 0 ? int(std::min<long long>(1000, done * 1000 / total)) : 0);
                        SetText(status_, L"Downloading… " + FormatBytes(done) + L" of " + FormatBytes(total));
                    });
                }, cancel_);
            } catch (const std::exception& error) {
                canceled = IsKind(error, ErrorKind::Canceled);
                failure = ErrorText(error);
            }
            Post([this, failure, canceled] {
                working_ = false;
                if (!failure) {
                    SetResult(IDOK);
                    Close();
                    return;
                }
                if (canceled) {
                    Close();
                    return;
                }
                Visible(bar_, false);
                SetTextColor(status_, kFirebrick);
                SetText(status_, L"Download failed: " + *failure);
                SetText(install_, L"Retry");
                Enable(install_, true);
                Enable(accept_, true);
            });
        }).detach();
    }

    bool licenseOnly_;
    HWND license_ = nullptr, accept_ = nullptr, status_ = nullptr, bar_ = nullptr, install_ = nullptr, close_ = nullptr;
    CancelToken cancel_;
    std::atomic<bool> working_{false};
};

// ---- the graphical setup: install, repair, update, restore and uninstall -----------------------------------------

class SetupForm : public Form, public Progress {
public:
    explicit SetupForm(const Options& options);
    bool Working() const { return working_; }
#ifdef NEBULA_UI_SNAPSHOTS
    void SnapshotWorkPage() {
        ShowChoose(false);
        Report(0.42, L"Compiling the game (8 at a time)", 0.3);
        for (int i = 0; i < 40; i++) Log(L"[" + Num(i) + L"/1200] Building CXX object game/CMakeFiles/RMGE01_game.dir/part" + Num(i) + L".cpp.obj");
        SetText(elapsed_, L"Elapsed 00:03:12");
    }
#endif

    void Report(double fraction, const wstring& text, double stepFraction) override {
        Post([this, fraction, text, stepFraction] {
            SetProgress(overall_, int(std::max(0.0, std::min(1000.0, fraction * 1000))));
            SetText(step_, text);
            SetMarquee(stepBar_, stepFraction < 0);
            if (stepFraction >= 0) SetProgress(stepBar_, int(std::max(0.0, std::min(1000.0, stepFraction * 1000))));
        });
    }

    void Log(const wstring& line) override {
        Post([this, line] {
            if (GetWindowTextLengthW(log_) > 200000) SetText(log_, GetText(log_).substr(100000));
            AppendText(log_, line + L"\r\n");
        });
    }

    bool AskToClose(const wstring& message) override {
        bool retry = false;
        Invoke([&] { retry = Message(this, message, L"Nebula Setup", MB_RETRYCANCEL | MB_ICONWARNING) == IDRETRY; });
        return retry;
    }

private:
    void BuildChoosePage();
    void BuildWorkPage();
    void UpdateChoices();
    void OnStart();
    void StartWork(InstallMode mode);
    void Finished(std::optional<wstring> failure, bool canceled);
    void Warn(const wstring& message) { Message(this, message, L"Nebula Setup", MB_OK | MB_ICONWARNING); }
    void CheckForUpdate();
    void ShowChoose(bool visible);
    HWND Add(std::vector<HWND>& page, HWND control) {
        page.push_back(control);
        return control;
    }
    wstring RootText() const { return root_ ? Trim(GetText(root_)) : layout_.root; }

    Options options_;
    InstallLayout layout_;
    std::optional<InstalledInfo> installed_;
    Json toolchainPins_;
    long long toolchainDownloadBytes_ = 0;
    int summaryTopBelowLicense_ = 0, licenseTop_ = 0;
    std::vector<HWND> choose_, work_;
    HWND input_ = nullptr, root_ = nullptr, log_ = nullptr, desktop_ = nullptr, license_ = nullptr, licenseLink_ = nullptr, dlss_ = nullptr,
         dlssLicense_ = nullptr, modeRepair_ = nullptr, modeRestore_ = nullptr, modeUninstall_ = nullptr, summary_ = nullptr,
         step_ = nullptr, elapsed_ = nullptr, overall_ = nullptr, stepBar_ = nullptr, start_ = nullptr, cancelButton_ = nullptr,
         launch_ = nullptr, openLog_ = nullptr;
    std::unique_ptr<CancelToken> cancel_;
    std::unique_ptr<InstallEngine> engine_;
    std::atomic<bool> working_{false};
    bool finished_ = false;
    bool choosing_ = true;
    ULONGLONG clockStart_ = 0;
};

static InstallLayout InitialLayout(const Options& options) {
    auto root = options.find(L"root");
    return InstallLayout(root != options.end() ? root->second : paths::RegisteredInstallRoot().value_or(paths::DefaultInstallRoot()));
}

SetupForm::SetupForm(const Options& options)
    : Form(L"Nebula Setup " + wstring(build::kVersion), 760, 640, ui::Font(9.5)), options_(options), layout_(InitialLayout(options)) {
    installed_ = InstalledInfo::Read(layout_);
    BuildChoosePage();
    BuildWorkPage();
    summaryTopBelowLicense_ = TopOf(summary_);
    licenseTop_ = TopOf(license_);
    ShowChoose(true);
    onShown = [this] {
        if (Has(options_, L"uninstall") && modeUninstall_) {
            SetChecked(modeRepair_, false);
            SetChecked(modeUninstall_, true);
            UpdateChoices();
        }
        if (Has(options_, L"update")) CheckForUpdate();
        else if (Has(options_, L"apply-update") && installed_) {
            SetChecked(modeRepair_, true);
            StartWork(InstallMode::Update);
        }
    };
}

void SetupForm::ShowChoose(bool visible) {
    choosing_ = visible;
    for (HWND control : choose_) Visible(control, visible);
    for (HWND control : work_) Visible(control, !visible);
    if (!visible) Visible(launch_, false);
    if (visible) UpdateChoices();
}

// Follows the chosen action and install folder: the start button names the action, and the license row shows only
// when Start would ask for it (an install or repair into a folder without the compiler).
void SetupForm::UpdateChoices() {
    if (!summary_ || !start_) return;
    InstallMode mode = installed_ ? ForPackage(installed_->Version, build::kVersion) : InstallMode::Install;
    bool restore = modeRestore_ && Checked(modeRestore_), uninstall = modeUninstall_ && Checked(modeUninstall_);
    SetText(start_, !installed_ ? L"Install" : restore ? L"Restore" : uninstall ? L"Uninstall" : mode == InstallMode::Update ? L"Update" : L"Repair");

    bool ready = false;
    try {
        Toolchain toolchain(toolchainPins_, InstallLayout(RootText()).Toolchains());
        ready = toolchain.IsReady();
    } catch (const std::exception&) {
    }
    bool installing = !installed_ || (modeRepair_ && Checked(modeRepair_));
    bool needed = installing && mode != InstallMode::Update && !ready;
    Visible(license_, needed && choosing_);
    Visible(licenseLink_, needed && choosing_);
    Move(summary_, 20, needed ? summaryTopBelowLicense_ : licenseTop_, 720, 170);
    SetText(summary_, (ready ? wstring(L"Setup compiles the game from your copy with the C++ compiler already installed.")
                             : L"Setup compiles the game from your copy and downloads a C++ compiler (" + FormatBytes(toolchainDownloadBytes_) +
                                   L") when needed.") +
                          L"\r\n\r\nRequires Windows 10 or 11 (64-bit), 8 GB of RAM, " + FormatBytes(InstallEngine::kRequiredFreeBytes) +
                          L" of free disk space and a DirectX 12 GPU.");
}

void SetupForm::BuildChoosePage() {
    HWND form = Handle();
    HWND title = Add(choose_, Label(form, L"Nebula", 20, 14, 700, 34));
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(ui::Font(17, FW_SEMIBOLD)), TRUE);
    Add(choose_, Label(form, L"Unofficial native PC port of Super Mario Galaxy (USA, RMGE01).", 20, 50, 720, 40));

    int y = 98;
    if (installed_) {
        Add(choose_, Label(form, L"Installed: Nebula " + installed_->Version +
                                     (installed_->Previous ? L" (previous " + *installed_->Previous + L" kept)" : wstring()),
                           20, y, 720, 22));
        y += 26;
        bool update = ForPackage(installed_->Version, build::kVersion) == InstallMode::Update;
        auto changed = [this] { UpdateChoices(); };
        modeRepair_ = Add(choose_, Radio(form, update ? L"Update to Nebula " + wstring(build::kVersion) + L" (keeps saves and settings)"
                                                      : L"Install or repair Nebula " + wstring(build::kVersion),
                                         28, y, 710, 24, true, changed));
        SetChecked(modeRepair_, true);
        y += 26;
        if (installed_->Previous) {
            modeRestore_ = Add(choose_, Radio(form, L"Restore the previous version (" + *installed_->Previous + L")", 28, y, 710, 24, false, changed));
            y += 26;
        }
        modeUninstall_ = Add(choose_, Radio(form, L"Uninstall (keeps saves and settings)", 28, y, 710, 24, false, changed));
        y += 26;
        Add(choose_, Button(form, L"Check for updates…", 28, y + 2, 170, 30, [this] { CheckForUpdate(); }));
        y += 42;
    }

    // An installed copy keeps the game it was compiled from; it is asked for only when that copy is gone.
    if (!installed_ || !InstallEngine::RetainedGameValid(layout_)) {
        Add(choose_, Label(form, !installed_ ? L"Your game: ISO, RVZ or extracted folder."
                                             : L"Your game: ISO, RVZ or extracted folder (the copy Setup kept is missing).",
                           20, y, 720, 22));
        y += 24;
        input_ = Add(choose_, Edit(form, L"", 20, y, 470, 26));
        Add(choose_, Button(form, L"Image file…", 498, y - 1, 116, 28, [this] {
            auto file = OpenFileDialog(this, L"Choose your Super Mario Galaxy disc image",
                                       {{L"Wii disc images (*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz)", L"*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz"},
                                        {L"All files (*.*)", L"*.*"}});
            if (file) SetText(input_, *file);
        }));
        Add(choose_, Button(form, L"Folder…", 620, y - 1, 116, 28, [this] {
            auto folder = FolderDialog(this, L"Choose the extracted game folder");
            if (folder) SetText(input_, *folder);
        }));
        y += 38;
    }

    if (!installed_) {
        Add(choose_, Label(form, L"Install folder:", 20, y, 720, 22));
        y += 24;
        root_ = Add(choose_, Edit(form, layout_.root, 20, y, 594, 26, false, false, [this] { UpdateChoices(); }));
        Add(choose_, Button(form, L"Browse…", 620, y - 1, 116, 28, [this] {
            auto folder = FolderDialog(this, L"Choose where to install Nebula");
            if (folder) SetText(root_, PathCombine(*folder, L"Nebula"));
        }));
        y += 38;
    } else {
        Add(choose_, Label(form, L"Install folder: " + layout_.root, 20, y, 720, 22));
        y += 32;
    }

    desktop_ = Add(choose_, Check(form, L"Create a desktop shortcut", 20, y, 700, 24));
    y += 28;

    if (dlss::RtxAdapterPresent() && !dlss::Installed()) {
        dlss_ = Add(choose_, Check(form, L"Install NVIDIA DLSS (" + FormatBytes(dlssdownload::kDownloadBytes) + L" download from NVIDIA)", 20, y, 620, 24,
                                   [this] {
                                       Enable(dlssLicense_, Checked(dlss_));
                                       if (!Checked(dlss_)) {
                                           SetChecked(dlssLicense_, false);
                                           ResetTextColor(dlssLicense_);
                                       }
                                   }));
        y += 26;
        dlssLicense_ = Add(choose_, Check(form, L"I accept the NVIDIA RTX SDKs license", 40, y, 600, 24, [this] { ResetTextColor(dlssLicense_); }));
        Enable(dlssLicense_, false);
        Add(choose_, Link(form, L"Read license", 644, y + 3, 100, 22, [this] {
            DlssInstallDialog dialog(true, this);
            dialog.ShowModal();
        }));
        y += 32;
    }

    toolchainPins_ = ParseJson(Widen(payload::ReadText(L"toolchain.json")));
    Toolchain toolchain(toolchainPins_, layout_.Toolchains());
    toolchainDownloadBytes_ = toolchain.DownloadBytes();
    wstring licenseUrl = toolchain.LicenseUrl();
    license_ = Add(choose_, Check(form, L"I accept the Microsoft Visual Studio Build Tools license", 20, y, 620, 24));
    licenseLink_ = Add(choose_, Link(form, L"Read license", 644, y + 3, 100, 22, [licenseUrl] {
        try { OpenWithShell(licenseUrl); } catch (const std::exception&) {}
    }));
    y += 32;

    summary_ = Add(choose_, Label(form, L"", 20, y, 720, 170));
    start_ = Add(choose_, Button(form, L"Install", 530, 596, 100, 32, [this] { OnStart(); }));
    Add(choose_, Button(form, L"Close", 640, 596, 100, 32, [this] { Close(); }));
    SetAccept(start_);
}

void SetupForm::BuildWorkPage() {
    HWND form = Handle();
    step_ = Add(work_, Label(form, L"", 20, 20, 720, 24));
    SendMessageW(step_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::Font(10.5, FW_SEMIBOLD)), TRUE);
    overall_ = Add(work_, Form::Progress(form, 20, 50, 720, 22));
    stepBar_ = Add(work_, Form::Progress(form, 20, 80, 720, 14));
    elapsed_ = Add(work_, Label(form, L"", 20, 100, 720, 22));
    log_ = Add(work_, Edit(form, L"", 20, 126, 720, 460, true, true));
    SendMessageW(log_, WM_SETFONT, reinterpret_cast<WPARAM>(ui::Font(8.5, FW_NORMAL, L"Consolas")), TRUE);
    SendMessageW(log_, EM_SETLIMITTEXT, 0, 0);
    cancelButton_ = Add(work_, Button(form, L"Cancel", 640, 596, 100, 32, [this] {
        if (finished_) {
            Close();
            return;
        }
        if (cancel_ && Message(this, L"Cancel setup?", L"Nebula Setup", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            cancel_->Cancel();
            Enable(cancelButton_, false);
            SetText(step_, L"Canceling…");
        }
    }));
    launch_ = Add(work_, Button(form, L"Play Nebula", 530, 596, 100, 32, [this] {
        if (engine_ && engine_->InstalledVersionDir) {
            try {
                StartProgram(PathCombine(*engine_->InstalledVersionDir, L"Nebula.exe"), L"", *engine_->InstalledVersionDir);
            } catch (const std::exception& error) {
                Message(this, ErrorText(error), L"Nebula Setup", MB_OK | MB_ICONERROR);
            }
        }
        Close();
    }));
    openLog_ = Add(work_, Button(form, L"Open log", 20, 596, 100, 32, [this] {
        if (!engine_) return;
        try {
            StartProgram(PathCombine(KnownFolder(FOLDERID_System), L"notepad.exe"), L"\"" + engine_->LogPath + L"\"", L"");
        } catch (const std::exception&) {
        }
    }));
}

void SetupForm::OnStart() {
    try {
        if (installed_ && modeUninstall_ && Checked(modeUninstall_)) {
            int answer = Message(this, L"Uninstall Nebula?\r\n\r\nYour saves, settings and shader cache in " + paths::DataRoot() +
                                           L" are kept. Choose No to also delete them.",
                                 L"Uninstall Nebula", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL) return;
            integration::Uninstall(layout_, answer == IDNO);
            Message(this, L"Nebula was uninstalled.", L"Nebula Setup");
            Close();
            return;
        }
        if (installed_ && modeRestore_ && Checked(modeRestore_)) {
            wstring version = integration::Rollback(layout_);
            Message(this, L"Nebula " + version + L" is active again.", L"Nebula Setup");
            Close();
            return;
        }
        layout_ = InstallLayout(RootText());
        installed_ = InstalledInfo::Read(layout_);
        StartWork(ForPackage(installed_ ? std::optional<wstring>(installed_->Version) : std::nullopt, build::kVersion));
    } catch (const std::exception& error) {
        Message(this, ErrorText(error), L"Nebula Setup", MB_OK | MB_ICONERROR);
    }
}

void SetupForm::StartWork(InstallMode mode) {
    std::optional<wstring> game;
    if (input_) {
        wstring text = Trim(GetText(input_));
        if (!text.empty()) game = text;
    }
    if (mode == InstallMode::Install && !game) { Warn(L"Choose your Super Mario Galaxy ISO, RVZ or extracted folder."); return; }
    if (game && !FileExists(*game) && !DirExists(*game)) { Warn(L"The selected game was not found: " + *game); return; }
    Toolchain toolchain(toolchainPins_, layout_.Toolchains());
    if (InstallEngine::BuildToolsLicenseRequired(mode, toolchain) && !Checked(license_)) {
        UpdateChoices();
        Warn(L"Accept the Microsoft Visual Studio Build Tools license to continue.");
        return;
    }
    if (dlss_ && Checked(dlss_) && !Checked(dlssLicense_)) {
        SetTextColor(dlssLicense_, kFirebrick);
        SetFocus(dlssLicense_);
        Warn(L"Accept the NVIDIA RTX SDKs license to install DLSS, or clear the DLSS option.");
        return;
    }
    wstring requirementsRoot = layout_.root.size() > 3 ? (DirName(layout_.root).empty() ? layout_.root : DirName(layout_.root)) : layout_.root;
    auto problems = InstallEngine::CheckRequirements(requirementsRoot);
    if (!problems.empty()) { Warn(Join(problems, L"\r\n")); return; }
    auto lowMemory = InstallEngine::LowMemoryWarning();
    if (lowMemory && Message(this, *lowMemory + L"\r\n\r\nInstall anyway?", L"Nebula Setup", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    InstallRequest request;
    request.Mode = mode;
    request.GameInput = game;
    request.InstallRoot = layout_.root;
    request.DesktopShortcut = Checked(desktop_);
    request.InstallDlss = dlss_ && Checked(dlss_);
    if (Has(options_, L"source")) request.SourceOverride = options_.at(L"source");
    long long jobs = 0;
    if (Has(options_, L"jobs") && ParseInt(options_.at(L"jobs"), jobs)) request.CompileJobs = int(jobs);

    ShowChoose(false);
    cancel_ = std::make_unique<CancelToken>();
    clockStart_ = GetTickCount64();
    StartTimer(1, 1000, [this] {
        ULONGLONG seconds = (GetTickCount64() - clockStart_) / 1000;
        wchar_t text[64];
        swprintf_s(text, L"Elapsed %02llu:%02llu:%02llu", seconds / 3600, (seconds / 60) % 60, seconds % 60);
        SetText(elapsed_, text);
    });
    try {
        engine_ = std::make_unique<InstallEngine>(request, *this, *cancel_);
    } catch (const std::exception& error) {
        Finished(ErrorText(error), false);
        return;
    }
    working_ = true;
    std::thread([this] {
        std::optional<wstring> failure;
        bool canceled = false;
        try {
            engine_->Run();
        } catch (const std::exception& error) {
            failure = ErrorText(error);
            canceled = IsKind(error, ErrorKind::Canceled);
        }
        working_ = false;
        Post([this, failure, canceled] { Finished(failure, canceled); });
    }).detach();
}

void SetupForm::Finished(std::optional<wstring> failure, bool canceled) {
    StopTimer(1);
    SetText(cancelButton_, L"Close");
    Enable(cancelButton_, true);
    finished_ = true;
    if (!failure) {
        SetText(step_, engine_->ModuleUpdateNotice.value_or(L"Nebula " + wstring(build::kVersion) + L" is installed."));
        if (engine_->DlssFailure)
            Message(this, L"NVIDIA DLSS was not installed: " + *engine_->DlssFailure + L"\r\n\r\nYou can install it later from the game's graphics settings.",
                    L"Nebula Setup", MB_OK | MB_ICONWARNING);
        SetMarquee(stepBar_, false);
        SetProgress(overall_, 1000);
        Visible(launch_, true);
        return;
    }
    SetText(step_, canceled ? L"Canceled." : L"Setup failed.");
    SetMarquee(stepBar_, false);
    if (!canceled) Message(this, *failure, L"Nebula Setup", MB_OK | MB_ICONERROR);
}

void SetupForm::CheckForUpdate() {
    HCURSOR wait = LoadCursorW(nullptr, IDC_WAIT), arrow = LoadCursorW(nullptr, IDC_ARROW);
    try {
        SetCursor(wait);
        auto release = updater::FindNewer();
        SetCursor(arrow);
        if (!release) {
            Message(this, L"Nebula " + wstring(build::kVersion) + L" is the newest release.", L"Nebula Update");
            return;
        }
        wstring notes = release->Notes.size() > 1200 ? release->Notes.substr(0, 1200) + L"…" : release->Notes;
        if (Message(this, L"Nebula " + release->Version + (release->Prerelease ? L" (prerelease)" : L"") + L" is available. You have " +
                              build::kVersion + L".\r\n\r\n" + notes +
                              L"\r\n\r\nDownload and install it? Your current version stays available until the new one is ready.",
                    L"Nebula Update", MB_YESNO | MB_ICONINFORMATION) != IDYES)
            return;
        SetCursor(wait);
        CancelToken never;
        wstring exe = updater::Download(*release, layout_.Staging(), never);
        SetCursor(arrow);
        StartProgram(exe, L"--apply-update --root " + QuoteArg(layout_.root), L"");
        Close();
    } catch (const std::exception& error) {
        SetCursor(arrow);
        Message(this, L"Could not check for updates: " + ErrorText(error), L"Nebula Update", MB_OK | MB_ICONWARNING);
    }
}

// ---- scripted runs for testing (progress goes to the setup log) --------------------------------------------------

class SilentProgress : public Progress {
public:
    void Report(double, const wstring&, double) override {}
    void Log(const wstring&) override {}
    bool AskToClose(const wstring&) override { return false; }
};

static int Unattended(const Options& options) {
    wstring root = Has(options, L"root") ? options.at(L"root") : paths::RegisteredInstallRoot().value_or(paths::DefaultInstallRoot());
    InstallLayout layout(root);
    try {
        if (Has(options, L"uninstall")) {
            integration::Uninstall(layout, Has(options, L"remove-user-data"));
            return 0;
        }
        if (Has(options, L"rollback")) {
            integration::Rollback(layout);
            return 0;
        }
        auto current = InstalledInfo::Read(layout);
        InstallRequest request;
        request.Mode = !current ? InstallMode::Install
                                : (Has(options, L"apply-update") ? InstallMode::Update : ForPackage(current->Version, build::kVersion));
        if (Has(options, L"input")) request.GameInput = options.at(L"input");
        request.InstallRoot = root;
        if (Has(options, L"source")) request.SourceOverride = options.at(L"source");
        request.DesktopShortcut = Has(options, L"desktop-shortcut");
        long long jobs = 0;
        if (Has(options, L"jobs") && ParseInt(options.at(L"jobs"), jobs)) request.CompileJobs = int(jobs);
        Toolchain toolchain(ParseJson(Widen(payload::ReadText(L"toolchain.json"))), layout.Toolchains());
        if (InstallEngine::BuildToolsLicenseRequired(request.Mode, toolchain) && !Has(options, L"accept-build-tools-license"))
            Fail(ErrorKind::InvalidOperation, L"Unattended installs that download the compiler must pass --accept-build-tools-license.");
        SilentProgress progress;
        CancelToken cancel;
        InstallEngine engine(request, progress, cancel);
        engine.Run();
        return 0;
    } catch (const std::exception& error) {
        try {
            CreateDirs(paths::SetupLogs());
            wstring log = PathCombine(paths::SetupLogs(), L"unattended-errors.log");
            std::string line = Narrow(UtcNowIso() + L" " + ErrorText(error) + L"\r\n");
            HANDLE file = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(file, line.data(), DWORD(line.size()), &written, nullptr);
                CloseHandle(file);
            }
        } catch (const std::exception&) {
        }
        return 1;
    }
}

static int Main() {
    std::vector<wstring> args = CommandLineArgs();
    Options options;
    for (size_t i = 0; i < args.size(); i++) {
        const wstring& arg = args[i];
        if (!StartsWith(arg, L"--")) continue;
        wstring key = ToLower(arg.substr(2));
        bool takesValue = key == L"input" || key == L"root" || key == L"source" || key == L"jobs" || key == L"finish-uninstall";
        bool hasValue = takesValue && i + 1 < args.size() && !StartsWith(args[i + 1], L"--");
        options[key] = hasValue ? args[++i] : L"";
    }
    if (Has(options, L"finish-uninstall") && Has(options, L"root")) {
        long long process = 0;
        if (ParseInt(options[L"finish-uninstall"], process)) integration::FinishUninstall(DWORD(process), options[L"root"]);
        return 0;
    }
    ui::Init();
    if (Has(options, L"install-dlss")) {
        if (dlss::Installed()) return 0;
        DlssInstallDialog dialog(false, nullptr);
        SetWindowPos(dialog.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        return dialog.ShowModal() == IDOK && dlss::Installed() ? 0 : 1;
    }
    if (Has(options, L"unattended")) return Unattended(options);
    try {
#ifdef NEBULA_UI_SNAPSHOTS
        if (auto directory = ui::SnapshotDirectory()) {
            SetupForm form(options);
            form.Show();
            ui::Pump(500);
            ui::SaveSnapshot(form.Handle(), PathCombine(*directory, L"setup-choose.bmp"));
            form.SnapshotWorkPage();
            ui::Pump(300);
            ui::SaveSnapshot(form.Handle(), PathCombine(*directory, L"setup-work.bmp"));
            DlssInstallDialog dialog(false, &form);
            dialog.Show();
            ui::Pump(300);
            ui::SaveSnapshot(dialog.Handle(), PathCombine(*directory, L"setup-dlss.bmp"));
            return 0;
        }
#endif
        SetupForm form(options);
        ui::Run(form);
        // Closed during work: the job objects end every compiler as the process leaves.
        if (form.Working()) ExitProcess(0);
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, ErrorText(error).c_str(), L"Nebula Setup", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}

}  // namespace nebula::setup

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return nebula::setup::Main(); }
