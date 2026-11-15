// Nebula-Setup.exe [--update [--yes] | --apply-update [--launch] | --uninstall | --rollback] [--input PATH] [--root PATH]
//   [--portable] [--desktop-shortcut] [--unattended] [--remove-user-data]
// Nebula-Setup.exe --finish-uninstall PID --root PATH (the temporary copy that removes Setup after an uninstall)
//
// --update checks GitHub for a newer release, downloads its Setup, checks its signature and version and runs it with
// --apply-update. --yes (the game's Update button, where the player already chose) asks nothing and starts Nebula again
// once the update is in place, or when there is nothing to install.
#include "setup_engine.h"
#include "text.h"
#include "ui.h"

#include <shlobj.h>

#include <thread>

namespace nebula::setup {

using namespace ui;
using text::Of;
using Options = std::map<wstring, wstring>;  // keys lower-cased

static bool Has(const Options& options, const wstring& key) { return options.count(key) != 0; }

static InstallLayout InitialLayout(const Options& options) {
    auto root = options.find(L"root");
    if (root != options.end()) return InstallLayout(root->second);
    return InstallLayout(paths::PortableRoot().value_or(paths::RegisteredInstallRoot().value_or(paths::DefaultInstallRoot())));
}

// Starts the install root's Nebula.exe; false when it cannot.
static bool StartNebula(const InstallLayout& layout) {
    try {
        StartProgram(layout.Starter(), L"", layout.root);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// ---- the graphical setup: install, repair, update, restore and uninstall -----------------------------------------

class SetupForm : public Form, public Progress {
public:
    explicit SetupForm(const Options& options);
    bool Working() const { return working_; }

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
        Invoke([&] { retry = Message(this, message, Of(L"Nebula Setup"), MB_RETRYCANCEL | MB_ICONWARNING) == IDRETRY; });
        return retry;
    }

private:
    void BuildChoosePage();
    void BuildWorkPage();
    void UpdateChoices();
    void UpdateKind();
    void OnStart();
    void StartWork(InstallMode mode);
    void Finished(std::optional<wstring> failure, bool canceled);
    void Warn(const wstring& message) { Message(this, message, Of(L"Nebula Setup"), MB_OK | MB_ICONWARNING); }
    void CheckForUpdate();
    void Downloaded(const wstring& exe);
    void UpdateEnded();
    void ShowChoose(bool visible);
    void StartClock();
    HWND Add(std::vector<HWND>& page, HWND control) {
        page.push_back(control);
        return control;
    }

    Options options_;
    InstallLayout layout_;
    std::optional<InstalledInfo> installed_;
    // The install in hand is a portable copy (this Setup is its own, or --root named one).
    bool portableInstalled_ = false;
    // The game's Update button started this run: no questions, and Nebula starts again at the end.
    bool fromGame_ = false;
    wstring installFolder_, portableFolder_;
    std::vector<HWND> choose_, work_;
    HWND input_ = nullptr, root_ = nullptr, folderLabel_ = nullptr, log_ = nullptr, desktop_ = nullptr, modeRepair_ = nullptr,
         modeRestore_ = nullptr, modeUninstall_ = nullptr, modeInstall_ = nullptr, modePortable_ = nullptr, step_ = nullptr,
         elapsed_ = nullptr, overall_ = nullptr, stepBar_ = nullptr, start_ = nullptr, cancelButton_ = nullptr, launch_ = nullptr,
         openLog_ = nullptr, openFolder_ = nullptr;
    std::unique_ptr<CancelToken> cancel_;
    std::unique_ptr<InstallEngine> engine_;
    std::atomic<bool> working_{false};
    bool finished_ = false;
    ULONGLONG clockStart_ = 0;
};

SetupForm::SetupForm(const Options& options)
    : Form(Of(L"Nebula Setup"), 760, 640, ui::Font(9.5)), options_(options), layout_(InitialLayout(options)) {
    portableInstalled_ = paths::IsPortableRoot(layout_.root);
    if (portableInstalled_) paths::UsePortableRoot(layout_.root);
    fromGame_ = Has(options_, L"update") && Has(options_, L"yes");
    installed_ = InstalledInfo::Read(layout_);
    BuildChoosePage();
    BuildWorkPage();
    ShowChoose(true);
    onShown = [this] {
        if (Has(options_, L"uninstall") && modeUninstall_) {
            SetChecked(modeRepair_, false);
            if (modeRestore_) SetChecked(modeRestore_, false);
            SetChecked(modeUninstall_, true);
            UpdateChoices();
        }
        if (Has(options_, L"update")) CheckForUpdate();
        // An update that needs the disc stays on this page, which asks for it; a package no newer than the installed
        // version is never applied as an update (no downgrade by --apply-update).
        else if (Has(options_, L"apply-update") && installed_ && InstallEngine::RetainedGameValid(layout_) &&
                 ForPackage(installed_->Version, build::kVersion) == InstallMode::Update) {
            StartWork(InstallMode::Update);
        }
    };
    onClosing = [this] {
        if (!working_) return true;
        // Closing during work cancels it; the window closes once the work has stopped.
        if (Message(this, Of(L"Cancel setup?"), Of(L"Nebula Setup"), MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
        if (cancel_) cancel_->Cancel();
        Enable(cancelButton_, false);
        SetText(step_, Of(L"Canceling…"));
        return false;
    };
}

void SetupForm::ShowChoose(bool visible) {
    for (HWND control : choose_) Visible(control, visible);
    for (HWND control : work_) Visible(control, !visible);
    if (!visible) {
        Visible(launch_, false);
        Visible(openFolder_, false);
    }
    if (visible) {
        if (desktop_) Visible(desktop_, !(modePortable_ && Checked(modePortable_)));
        UpdateChoices();
    }
}

// The start button names the chosen action.
void SetupForm::UpdateChoices() {
    if (!start_) return;
    InstallMode mode = installed_ ? ForPackage(installed_->Version, build::kVersion) : InstallMode::Install;
    bool restore = modeRestore_ && Checked(modeRestore_), uninstall = modeUninstall_ && Checked(modeUninstall_);
    SetText(start_, !installed_ ? Of(L"Install")
                    : restore   ? Of(L"Restore")
                    : uninstall ? (portableInstalled_ ? Of(L"Remove") : Of(L"Uninstall"))
                    : mode == InstallMode::Update ? Of(L"Update")
                                                  : Of(L"Repair"));
}

// On this PC or portable: the folder, its label and the desktop shortcut follow the choice. A folder the player typed
// or chose is kept when the choice changes back.
void SetupForm::UpdateKind() {
    bool portable = Checked(modePortable_);
    if (portable) {
        installFolder_ = GetText(root_);
        SetText(root_, portableFolder_);
    } else {
        portableFolder_ = GetText(root_);
        SetText(root_, installFolder_);
    }
    SetText(folderLabel_, portable ? Of(L"Portable folder:") : Of(L"Install folder:"));
    Visible(desktop_, !portable);
}

void SetupForm::BuildChoosePage() {
    HWND form = Handle();
    HWND title = Add(choose_, Label(form, L"Nebula", 20, 14, 700, 34));
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(ui::Font(17, FW_SEMIBOLD)), TRUE);
    Add(choose_, Label(form, Of(L"Unofficial PC port of Super Mario Galaxy"), 20, 50, 720, 40));

    int y = 98;
    if (installed_) {
        Add(choose_, Label(form, (portableInstalled_ ? Of(L"Portable Nebula {0}", {installed_->Version}) : Of(L"Installed: Nebula {0}", {installed_->Version})) +
                                     (installed_->Previous ? Of(L" (previous {0} kept)", {*installed_->Previous}) : wstring()),
                           20, y, 720, 22));
        y += 26;
        bool update = ForPackage(installed_->Version, build::kVersion) == InstallMode::Update;
        auto changed = [this] { UpdateChoices(); };
        modeRepair_ = Add(choose_, Radio(form, update ? Of(L"Update to Nebula {0}", {build::kVersion})
                                                      : Of(L"Reinstall Nebula {0}", {build::kVersion}),
                                         28, y, 710, 24, true, changed));
        SetChecked(modeRepair_, true);
        y += 26;
        if (installed_->Previous) {
            modeRestore_ = Add(choose_, Radio(form, Of(L"Restore Nebula {0}", {*installed_->Previous}), 28, y, 710, 24, false, changed));
            y += 26;
        }
        modeUninstall_ = Add(choose_, Radio(form, portableInstalled_ ? Of(L"Remove portable Nebula")
                                                                     : Of(L"Uninstall"),
                                            28, y, 710, 24, false, changed));
        y += 26;
        Add(choose_, Button(form, Of(L"Check for updates…"), 28, y + 2, 170, 30, [this] { CheckForUpdate(); }));
        y += 42;
    } else {
        // On this PC (Start menu, Windows' list of apps) or portable (one folder, nothing else on the PC changes).
        modeInstall_ = Add(choose_, Radio(form, Of(L"Install on this PC"), 20, y, 720, 24, true,
                                          [this] { UpdateKind(); }));
        SetChecked(modeInstall_, true);
        y += 26;
        modePortable_ = Add(choose_, Radio(form, Of(L"Portable (choose a folder)"), 20, y, 720,
                                           24, false, [this] { UpdateKind(); }));
        y += 34;
    }

    // An install keeps its packed game; the game is asked for only when that is gone.
    if (!installed_ || !InstallEngine::RetainedGameValid(layout_)) {
        Add(choose_, Label(form, !installed_ ? Of(L"Game disc image or folder")
                                             : Of(L"Game disc image or folder (required again for this version)"),
                           20, y, 720, 22));
        y += 24;
        input_ = Add(choose_, Edit(form, L"", 20, y, 470, 26));
        Add(choose_, Button(form, Of(L"Disc image…"), 498, y - 1, 116, 28, [this] {
            auto file = OpenFileDialog(this, Of(L"Choose your Super Mario Galaxy disc image"),
                                       {{Of(L"Wii disc images (*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz)"), L"*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz"},
                                        {Of(L"All files (*.*)"), L"*.*"}});
            if (file) SetText(input_, *file);
        }));
        Add(choose_, Button(form, Of(L"Folder…"), 620, y - 1, 116, 28, [this] {
            auto folder = FolderDialog(this, Of(L"Choose the extracted game folder"));
            if (folder) SetText(input_, *folder);
        }));
        y += 38;
    }

    installFolder_ = layout_.root;
    // A portable copy goes beside this Setup unless the player chooses another folder.
    portableFolder_ = PathCombine(ModuleDirectory(), L"Nebula");
    if (!installed_) {
        folderLabel_ = Add(choose_, Label(form, Of(L"Install folder:"), 20, y, 720, 22));
        y += 24;
        root_ = Add(choose_, Edit(form, layout_.root, 20, y, 594, 26));
        Add(choose_, Button(form, Of(L"Browse…"), 620, y - 1, 116, 28, [this] {
            auto folder = FolderDialog(this, Checked(modePortable_) ? Of(L"Choose a folder for portable Nebula") : Of(L"Choose where to install Nebula"));
            if (folder) SetText(root_, PathCombine(*folder, L"Nebula"));
        }));
        y += 38;
    } else {
        Add(choose_, Label(form, Of(L"Install folder: {0}", {layout_.root}), 20, y, 720, 22));
        y += 32;
    }

    // A portable copy makes no shortcut (it would point at one place; the folder may move).
    if (!portableInstalled_) {
        desktop_ = Add(choose_, Check(form, Of(L"Create a desktop shortcut"), 20, y, 700, 24));
        y += 28;
    }

    Add(choose_, Label(form, Of(L"Requires Windows 10 or 11 (64-bit), 8 GB of RAM and {0} of free space.",
                                {FormatBytes(InstallEngine::kRequiredFreeBytes)}),
                       20, y, 720, std::max(40, 590 - y)));
    start_ = Add(choose_, Button(form, Of(L"Install"), 530, 596, 100, 32, [this] { OnStart(); }));
    Add(choose_, Button(form, Of(L"Close"), 640, 596, 100, 32, [this] { Close(); }));
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
    cancelButton_ = Add(work_, Button(form, Of(L"Cancel"), 640, 596, 100, 32, [this] { Close(); }));
    launch_ = Add(work_, Button(form, Of(L"Play Nebula"), 530, 596, 100, 32, [this] {
        if (!StartNebula(layout_)) Message(this, Of(L"Nebula.exe is in {0}.", {layout_.root}), Of(L"Nebula Setup"), MB_OK | MB_ICONWARNING);
        Close();
    }));
    // A portable copy's folder, where its Nebula.exe is.
    openFolder_ = Add(work_, Button(form, Of(L"Open folder"), 410, 596, 110, 32, [this] {
        try {
            OpenWithShell(layout_.root);
        } catch (const std::exception&) {
        }
    }));
    openLog_ = Add(work_, Button(form, Of(L"Open log"), 20, 596, 100, 32, [this] {
        if (!engine_ || engine_->LogPath.empty()) return;
        try {
            StartProgram(PathCombine(KnownFolder(FOLDERID_System), L"notepad.exe"), QuoteArg(engine_->LogPath), L"");
        } catch (const std::exception&) {
        }
    }));
}

void SetupForm::OnStart() {
    try {
        if (installed_ && modeUninstall_ && Checked(modeUninstall_)) {
            int answer = Message(this, (portableInstalled_ ? Of(L"Remove this portable copy of Nebula?") : Of(L"Uninstall Nebula?")) + L"\r\n\r\n" +
                                           Of(L"Saves and settings in {0} are kept. Choose No to delete them too.", {paths::DataRoot()}),
                                 portableInstalled_ ? Of(L"Remove portable Nebula") : Of(L"Uninstall Nebula"), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL) return;
            integration::Uninstall(layout_, answer == IDNO);
            Message(this, portableInstalled_ ? Of(L"Portable Nebula was removed.") : Of(L"Nebula was uninstalled."), Of(L"Nebula Setup"));
            Close();
            return;
        }
        if (installed_ && modeRestore_ && Checked(modeRestore_)) {
            wstring version = integration::Rollback(layout_);
            Message(this, Of(L"Nebula {0} was restored.", {version}), Of(L"Nebula Setup"));
            Close();
            return;
        }
        if (root_) {
            wstring folder = Trim(GetText(root_));
            if (folder.empty()) {
                Warn(Of(L"Choose a folder for Nebula."));
                return;
            }
            layout_ = InstallLayout(folder);
            if (!installed_ && Checked(modePortable_)) {
                // A portable copy goes into an empty folder, or updates the portable copy already there; never into an
                // installed Nebula or a folder of other files.
                if (DirExists(layout_.root) && !paths::IsPortableRoot(layout_.root) && !IsDirectoryEmpty(layout_.root)) {
                    Warn(Of(L"{0} is not empty. Choose an empty folder.", {layout_.root}));
                    return;
                }
                portableInstalled_ = true;
            } else if (!installed_ && paths::IsPortableRoot(layout_.root)) {
                Warn(Of(L"{0} contains portable Nebula. Choose Portable to update it.", {layout_.root}));
                return;
            }
        }
        installed_ = InstalledInfo::Read(layout_);
        StartWork(ForPackage(installed_ ? std::optional<wstring>(installed_->Version) : std::nullopt, build::kVersion));
    } catch (const std::exception& error) {
        Message(this, ErrorText(error), Of(L"Nebula Setup"), MB_OK | MB_ICONERROR);
    }
}

void SetupForm::StartClock() {
    clockStart_ = GetTickCount64();
    StartTimer(1, 1000, [this] {
        ULONGLONG seconds = (GetTickCount64() - clockStart_) / 1000;
        wchar_t clock[32];
        swprintf_s(clock, L"%02llu:%02llu:%02llu", seconds / 3600, (seconds / 60) % 60, seconds % 60);
        SetText(elapsed_, Of(L"Elapsed {0}", {clock}));
    });
}

void SetupForm::StartWork(InstallMode mode) {
    std::optional<wstring> game;
    if (input_) {
        wstring path = Trim(GetText(input_));
        if (!path.empty()) game = path;
    }
    if (mode == InstallMode::Install && !game) {
        Warn(Of(L"Choose a Super Mario Galaxy disc image or folder."));
        return;
    }
    if (game && !FileExists(*game) && !DirExists(*game)) {
        Warn(Of(L"The selected game was not found: {0}", {*game}));
        return;
    }
    // The drive is checked where the folder will be: its parent when the folder does not exist yet.
    auto problems = InstallEngine::CheckRequirements(layout_.root.size() > 3 && !DirName(layout_.root).empty() ? DirName(layout_.root) : layout_.root);
    if (!problems.empty()) {
        Warn(Join(problems, L"\r\n"));
        return;
    }

    InstallRequest request;
    request.Mode = mode;
    request.GameInput = game;
    request.InstallRoot = layout_.root;
    request.DesktopShortcut = desktop_ && Checked(desktop_) && !portableInstalled_;
    request.Portable = portableInstalled_;

    ShowChoose(false);
    cancel_ = std::make_unique<CancelToken>();
    StartClock();
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
        Post([this, failure, canceled] {
            working_ = false;
            Finished(failure, canceled);
        });
    }).detach();
}

void SetupForm::Finished(std::optional<wstring> failure, bool canceled) {
    StopTimer(1);
    SetText(cancelButton_, Of(L"Close"));
    Enable(cancelButton_, true);
    finished_ = true;
    SetMarquee(stepBar_, false);
    if (!failure) {
        SetText(step_, portableInstalled_ ? Of(L"Portable Nebula {0} is installed in {1}.", {build::kVersion, layout_.root})
                                          : Of(L"Nebula {0} is installed.", {build::kVersion}));
        SetProgress(overall_, 1000);
        Visible(openFolder_, portableInstalled_);
        Visible(launch_, true);
        if (Has(options_, L"launch") && StartNebula(layout_)) Close();
        return;
    }
    SetText(step_, canceled ? Of(L"Canceled.") : Of(L"Setup failed."));
    if (!canceled) Message(this, *failure, Of(L"Nebula Setup"), MB_OK | MB_ICONERROR);
}

// The check and the download run off the window's thread; the work page shows them.
void SetupForm::CheckForUpdate() {
    ShowChoose(false);
    Visible(log_, false);
    Visible(openLog_, false);
    SetText(step_, Of(L"Checking for updates…"));
    SetText(elapsed_, L"");
    SetMarquee(stepBar_, true);
    SetProgress(overall_, 0);
    cancel_ = std::make_unique<CancelToken>();
    working_ = true;
    std::thread([this] {
        std::optional<ReleaseInfo> release;
        std::optional<wstring> failure;
        try {
            release = updater::FindNewer();
        } catch (const std::exception& error) {
            failure = ErrorText(error);
        }
        Post([this, release, failure] {
            working_ = false;
            if (failure) {
                Message(this, Of(L"Could not check for updates: {0}", {*failure}), Of(L"Nebula Update"), MB_OK | MB_ICONWARNING);
                UpdateEnded();
                return;
            }
            if (!release) {
                if (!fromGame_) Message(this, Of(L"Nebula {0} is up to date.", {build::kVersion}), Of(L"Nebula Update"));
                UpdateEnded();
                return;
            }
            if (!fromGame_) {
                wstring question = (release->Prerelease ? Of(L"Nebula {0} (prerelease) is available. You have {1}.", {release->Version, build::kVersion})
                                                        : Of(L"Nebula {0} is available. You have {1}.", {release->Version, build::kVersion})) +
                                   L"\r\n\r\n" + updater::PlainNotes(release->Notes, 1200) + L"\r\n\r\n" +
                                   Of(L"Download and install it now?");
                if (Message(this, question, Of(L"Nebula Update"), MB_YESNO | MB_ICONINFORMATION) != IDYES) {
                    UpdateEnded();
                    return;
                }
            }
            SetText(step_, Of(L"Downloading Nebula {0}", {release->Version}));
            SetMarquee(stepBar_, release->SetupSize <= 0);
            working_ = true;
            const ReleaseInfo info = *release;
            std::thread([this, info] {
                std::optional<wstring> exe, error;
                bool canceled = false;
                try {
                    CreateDirs(layout_.Staging());
                    exe = updater::Download(info, layout_.Staging(),
                                            [this, info](long long bytes) {
                                                Post([this, info, bytes] {
                                                    if (info.SetupSize <= 0) return;
                                                    int part = int(std::min<long long>(1000, bytes * 1000 / info.SetupSize));
                                                    SetProgress(overall_, part);
                                                    SetProgress(stepBar_, part);
                                                    SetText(elapsed_, Of(L"{0} of {1}", {FormatBytes(bytes), FormatBytes(info.SetupSize)}));
                                                });
                                            },
                                            *cancel_);
                } catch (const std::exception& failure) {
                    canceled = IsKind(failure, ErrorKind::Canceled);
                    error = ErrorText(failure);
                }
                Post([this, exe, error, canceled] {
                    working_ = false;
                    if (exe) {
                        Downloaded(*exe);
                        return;
                    }
                    if (!canceled) Message(this, Of(L"Could not check for updates: {0}", {error.value_or(L"")}), Of(L"Nebula Update"), MB_OK | MB_ICONWARNING);
                    UpdateEnded();
                });
            }).detach();
        });
    }).detach();
}

// The checked Setup installs the update from its own copy; this one ends.
void SetupForm::Downloaded(const wstring& exe) {
    try {
        StartProgram(exe, L"--apply-update --root " + QuoteArg(layout_.root) + (fromGame_ ? L" --launch" : L""), DirName(exe));
    } catch (const std::exception& error) {
        Message(this, Of(L"Could not check for updates: {0}", {ErrorText(error)}), Of(L"Nebula Update"), MB_OK | MB_ICONWARNING);
        UpdateEnded();
        return;
    }
    Close();
}

// No update installed: the game's Update button gets Nebula back; otherwise the choices return (or, opened only to
// update, the window closes).
void SetupForm::UpdateEnded() {
    SetMarquee(stepBar_, false);
    Visible(log_, true);
    Visible(openLog_, true);
    if (fromGame_) {
        StartNebula(layout_);
        Close();
        return;
    }
    if (Has(options_, L"update")) {
        Close();
        return;
    }
    ShowChoose(true);
}

// ---- scripted runs for testing (progress goes to the setup log) --------------------------------------------------

class SilentProgress : public Progress {
public:
    void Report(double, const wstring&, double) override {}
    void Log(const wstring&) override {}
    bool AskToClose(const wstring&) override { return false; }
};

static int Unattended(const Options& options) {
    InstallLayout layout = InitialLayout(options);
    try {
        if (Has(options, L"portable") || paths::IsPortableRoot(layout.root)) paths::UsePortableRoot(layout.root);
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
        request.Mode = !current ? InstallMode::Install : ForPackage(current->Version, build::kVersion);
        if (Has(options, L"apply-update") && request.Mode != InstallMode::Update)
            Fail(ErrorKind::InvalidOperation, L"--apply-update needs a package newer than the installed Nebula.");
        if (Has(options, L"input")) request.GameInput = options.at(L"input");
        request.InstallRoot = layout.root;
        request.DesktopShortcut = Has(options, L"desktop-shortcut");
        request.Portable = Has(options, L"portable");
        if (request.Portable && DirExists(layout.root) && !paths::IsPortableRoot(layout.root) && !IsDirectoryEmpty(layout.root))
            Fail(ErrorKind::InvalidOperation, L"A portable copy goes into an empty folder: " + layout.root);
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
        bool takesValue = key == L"input" || key == L"root" || key == L"finish-uninstall";
        bool hasValue = takesValue && i + 1 < args.size() && !StartsWith(args[i + 1], L"--");
        options[key] = hasValue ? args[++i] : L"";
    }
    if (Has(options, L"finish-uninstall") && Has(options, L"root")) {
        long long process = 0;
        if (ParseInt(options[L"finish-uninstall"], process)) integration::FinishUninstall(DWORD(process), options[L"root"]);
        return 0;
    }
    ui::Init();
    if (Has(options, L"unattended")) return Unattended(options);
    try {
        SetupForm form(options);
#ifdef NEBULA_UI_SNAPSHOTS
        form.Show();
        ui::Pump(500);
        ui::SaveSnapshot(form.Handle(), PathCombine(ModuleDirectory(), L"setup-choose.bmp"));
#else
        ui::Run(form);
        // Closed during work: the job object ends nebula-import as the process leaves.
        if (form.Working()) ExitProcess(0);
#endif
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, ErrorText(error).c_str(), Of(L"Nebula Setup").c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}

}  // namespace nebula::setup

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return nebula::setup::Main(); }
