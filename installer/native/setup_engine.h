// Nebula Setup's work: the payload it carries, installs, repairs and updates, the build toolchain, the exact source,
// shortcuts and uninstall, updates from GitHub, and the DLSS download.
#pragma once

#include "archive.h"

namespace nebula::setup {

// Files embedded in Nebula-Setup.exe by the release build (one zip resource).
namespace payload {
bool Has(const wstring& name);
std::string ReadText(const wstring& name);
void Extract(const wstring& name, const wstring& path);  // NotFound when the package does not carry it
Bytes ReadBytes(const wstring& name);
}  // namespace payload

enum class InstallMode { Install, Repair, Update };

int CompareVersions(const wstring& a, const wstring& b);
InstallMode ForPackage(const std::optional<wstring>& installedVersion, const wstring& packageVersion);

struct InstallRequest {
    InstallMode Mode = InstallMode::Install;
    std::optional<wstring> GameInput;       // image or extracted folder; none to reuse the retained game files
    wstring InstallRoot;
    std::optional<wstring> SourceOverride;  // local source zip/folder (testing)
    bool DesktopShortcut = false;
    int CompileJobs = 0;
    bool InstallDlss = false;
};

class Progress {
public:
    virtual ~Progress() = default;
    // Overall fraction 0..1, current step, step fraction (negative = unknown).
    virtual void Report(double overall, const wstring& step, double stepFraction) = 0;
    virtual void Log(const wstring& line) = 0;
    // Shows the message and waits; true to check again, false to give up.
    virtual bool AskToClose(const wstring& message) = 0;
};

// The currently installed version (current.json and its install.json).
struct InstalledInfo {
    wstring Version;
    std::optional<wstring> Previous;
    wstring Directory;
    Json Install;
    static std::optional<InstalledInfo> Read(const InstallLayout& layout);
    wstring ModuleKey() const;
    wstring ModuleSourceCommit() const;
    wstring ModuleIsa() const;
    long long ModuleGeneration() const;
    bool FilesIntact(const std::vector<wstring>& names) const;
    void CopyFiles(const wstring& target, const std::vector<wstring>& names) const;
    std::vector<wstring> RuntimeDllNames() const;
};

// The pinned build toolchain: clang-cl and lld-link (llvm-mingw), Microsoft's C++ runtime headers and libraries and
// the Windows SDK, plus CMake and Ninja; every download checked against its pinned SHA-256.
class Toolchain {
public:
    Toolchain(const Json& pins, const wstring& toolchainsDirectory);
    wstring Id() const { return id_; }
    wstring LicenseUrl() const;
    long long DownloadBytes() const;
    bool IsReady();
    void Ensure(const std::function<void(const wstring&, double)>& progress, const std::function<void(const wstring&)>& log,
                const CancelToken& cancel);
    Environment BuildEnvironment();
    std::vector<wstring> CMakeToolArguments();
    std::vector<wstring> RuntimeDlls();
    wstring LldLink() const { return PathCombine(root_, L"llvm", L"bin", L"lld-link.exe"); }
    wstring CMake() const { return PathCombine(root_, L"cmake", L"bin", L"cmake.exe"); }

private:
    wstring ClangCl() const { return PathCombine(root_, L"llvm", L"bin", L"clang-cl.exe"); }
    wstring Ninja() const { return PathCombine(root_, L"ninja", L"ninja.exe"); }
    wstring Rc() const { return PathCombine(sdkDir_, L"bin", sdkVersion_, L"x64", L"rc.exe"); }
    wstring Mt() const { return PathCombine(sdkDir_, L"bin", sdkVersion_, L"x64", L"mt.exe"); }
    wstring Marker() const { return PathCombine(root_, L"toolchain-ready.json"); }
    wstring MsvcCompilerVersion() const;
    bool Locate(bool strict);

    Json pins_;
    wstring id_, root_, msvcDir_, sdkDir_, sdkVersion_, redistDir_;
};

wstring SafeJoin(const wstring& root, const wstring& relative);

class InstallEngine {
public:
    static const int kContentFormatVersion = 1;
    static const int kModuleGeneration = 3;
    static const long long kRequiredFreeBytes = 16LL << 30;
    static const unsigned long long kMinimumMemory = 7ULL << 30;

    InstallEngine(InstallRequest request, Progress& progress, CancelToken& cancel);
    ~InstallEngine();
    void Run();

    wstring LogPath;
    std::optional<wstring> InstalledVersionDir;
    std::optional<wstring> ModuleUpdateNotice;
    std::optional<wstring> DlssFailure;

    static std::vector<wstring> CheckRequirements(const wstring& installRoot);
    static bool BuildToolsLicenseRequired(InstallMode mode, Toolchain& toolchain);
    static std::optional<wstring> LowMemoryWarning();
    static bool RetainedGameValid(const InstallLayout& layout);
    static wstring RuntimeNativeIsa();
    static bool HostSupportsAvx2();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Start Menu shortcuts, the Add/Remove Programs entry, rollback and uninstall.
namespace integration {
std::vector<wstring> Register(const InstallLayout& layout, const wstring& version, bool desktop);
wstring Rollback(const InstallLayout& layout);
void Uninstall(const InstallLayout& layout, bool removeUserData);
void FinishUninstall(DWORD setupProcess, const wstring& root);
void RequireClosed(const wstring& root);
}  // namespace integration

// Updates through the GitHub releases of the Nebula repository; a downloaded setup runs only when its RSA-SHA256
// signature verifies against the release key built into this setup.
struct ReleaseInfo {
    wstring Version, Tag, Notes, SetupUrl, SignatureUrl;
    bool Prerelease = false;
};
namespace updater {
std::optional<ReleaseInfo> FindNewer();
wstring Download(const ReleaseInfo& release, const wstring& directory, const CancelToken& cancel);
}  // namespace updater

// NVIDIA DLSS from NVIDIA's Streamline 2.14.1 release on GitHub: only the files the runtime loads are read out of the
// release zip with ranged requests, each checked against its SHA-256.
namespace dlssdownload {
extern const long long kDownloadBytes;
wstring LicenseText(const CancelToken& cancel);
void Install(const std::function<void(long long, long long)>& progress, const CancelToken& cancel);
}  // namespace dlssdownload

}  // namespace nebula::setup
