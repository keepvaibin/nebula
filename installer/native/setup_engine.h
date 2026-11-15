// Nebula Setup's work: the payload it carries, install, repair and update, shortcuts, rollback and uninstall, and
// updates from GitHub.
#pragma once

#include "archive.h"

namespace nebula::setup {

// The files build-native-setup.ps1 packs into Nebula-Setup.exe (one zip, the PAYLOAD resource): app/... is a version
// folder as installed (the game, its modules, nebula-import, the compilers mods use, luma/), start/Nebula.exe the
// install root's Nebula.exe.
namespace payload {
bool Has(const wstring& name);
// Extracts one entry; NotFound when the package does not carry it.
void Extract(const wstring& name, const wstring& path);
// Extracts every entry under `prefix` (ending in '/') into `folder`; the count of files.
size_t ExtractTree(const wstring& prefix, const wstring& folder, const CancelToken& cancel);
}  // namespace payload

enum class InstallMode { Install, Repair, Update };

// MAJOR.MINOR.PATCH[-beta.N]; a release sorts after its betas.
int CompareVersions(const wstring& a, const wstring& b);
bool IsBetaVersion(const wstring& version);
// A package newer than the installed version updates it; the same or an older one repairs it.
InstallMode ForPackage(const std::optional<wstring>& installedVersion, const wstring& packageVersion);

struct InstallRequest {
    InstallMode Mode = InstallMode::Install;
    std::optional<wstring> GameInput;  // ISO, RVZ or other image, or an extracted folder; none keeps the packed game
    wstring InstallRoot;
    bool DesktopShortcut = false;
    // A portable copy: everything (saves and settings too) in InstallRoot, nothing registered with Windows.
    bool Portable = false;
};

class Progress {
public:
    virtual ~Progress() = default;
    // Overall fraction 0..1, the current step, the step's fraction (negative: unknown).
    virtual void Report(double overall, const wstring& step, double stepFraction) = 0;
    virtual void Log(const wstring& line) = 0;
    // Shows the message and waits; true to check again, false to give up.
    virtual bool AskToClose(const wstring& message) = 0;
};

// The installed version (current.json), when its folder holds a readable install.json.
struct InstalledInfo {
    wstring Version;
    std::optional<wstring> Previous;
    static std::optional<InstalledInfo> Read(const InstallLayout& layout);
};

// Nebula's programs that may run from an install: the game's four builds, the 1.x and 2.0 beta programs.
const std::vector<wstring>& RunningPrograms();

class InstallEngine {
public:
    // Bump when nebula-import's content folder changes shape. 1: the raw files (cooked from on update); 2 to 6: later
    // shapes of the store, which need the disc again.
    static constexpr long long kContentFormatVersion = 6;
    static constexpr long long kCookableContentFormatVersion = 1;
    // The packed game (3.2 GB) and its staging copy for a first install.
    static constexpr long long kRequiredFreeBytes = 8LL << 30;

    InstallEngine(InstallRequest request, Progress& progress, CancelToken& cancel);
    ~InstallEngine();
    void Run();

    wstring LogPath;
    std::optional<wstring> InstalledVersionDir;

    static std::vector<wstring> CheckRequirements(const wstring& installRoot);
    // The packed game an install keeps is intact, kept aside, can be cooked from or packed again from where it came
    // from, so an update or repair needs no game.
    static bool RetainedGameValid(const InstallLayout& layout);
    // A content folder's format; -1 without its marker or its game.pak.
    static long long ContentFormatOf(const wstring& content);
    // The content format a version folder reads; 0 when it does not say, -1 when it cannot be read.
    static long long VersionContentFormat(const wstring& versionDir);
    // A version folder of the recompiled game (1.x): its runtime or its recompiled modules.
    static bool HoldsRecompiledGame(const wstring& versionDir);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Start menu shortcuts, file and link associations, the Add/Remove Programs entry, rollback and uninstall.
namespace integration {
// Shortcuts and registry entries for `version` (a portable copy: none). Shortcuts that could not be written are
// returned, not thrown: Nebula runs without them.
std::vector<wstring> Register(const InstallLayout& layout, const wstring& version, bool desktop);
// Switches back to the kept previous version, with the game files it reads; returns it. A refusal changes nothing.
wstring Rollback(const InstallLayout& layout);
// Removes the program; saves, settings and caches stay unless asked.
void Uninstall(const InstallLayout& layout, bool removeUserData);
// The temporary copy's part of an uninstall: once Setup (process `setupProcess`) has ended, its copy in the install
// folder and the emptied folder go.
void FinishUninstall(DWORD setupProcess, const wstring& root);
void RequireClosed(const wstring& root);
}  // namespace integration

// Updates through the GitHub releases of the Nebula repository; a downloaded Setup runs only when its RSA-SHA256
// signature verifies against the release key built into this Setup and the version inside it is the release's.
struct ReleaseInfo {
    wstring Version, Tag, Notes, SetupUrl, SignatureUrl;
    long long SetupSize = 0;
    bool Prerelease = false;
};
namespace updater {
std::optional<ReleaseInfo> FindNewer();
// Downloads and checks the release's Setup into `directory`; its path. `onBytes` gets the bytes so far.
wstring Download(const ReleaseInfo& release, const wstring& directory, const std::function<void(long long)>& onBytes,
                 const CancelToken& cancel);
// Release notes as GitHub holds them (Markdown) as plain text, cut at a line end before `limit` characters.
wstring PlainNotes(const wstring& markdown, size_t limit);
}  // namespace updater

}  // namespace nebula::setup
