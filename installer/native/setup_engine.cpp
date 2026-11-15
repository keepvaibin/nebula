#include "setup_engine.h"

#include "text.h"

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <versionhelpers.h>

#include <algorithm>
#include <mutex>

#pragma comment(lib, "version.lib")

namespace nebula::setup {

using text::Of;

// ---- payload -----------------------------------------------------------------------------------------------------

namespace payload {
namespace {
std::mutex g_lock;

ZipReader* Archive() {
    static std::unique_ptr<ZipReader> archive = [] {
        HMODULE module = GetModuleHandleW(nullptr);
        HRSRC resource = FindResourceW(module, L"PAYLOAD", MAKEINTRESOURCEW(10));
        if (!resource) Fail(ErrorKind::NotFound, L"This Setup carries no embedded files.");
        const void* data = LockResource(LoadResource(module, resource));
        return ZipReader::OpenMemory(data, SizeofResource(module, resource));
    }();
    return archive.get();
}

// A payload name as a path under `folder`: relative, no "." or ".." parts.
wstring SafePath(const wstring& folder, const wstring& relative) {
    wstring path = Slashes(relative);
    if (path.empty() || IsRooted(path) || path.find(L':') != wstring::npos) Fail(ErrorKind::InvalidData, L"The Setup holds a bad file name: " + relative);
    for (const wstring& part : Split(path, L'\\')) {
        if (part.empty() || part == L"." || part == L"..") Fail(ErrorKind::InvalidData, L"The Setup holds a bad file name: " + relative);
    }
    return PathCombine(folder, path);
}
}  // namespace

bool Has(const wstring& name) {
    std::lock_guard<std::mutex> guard(g_lock);
    try {
        return Archive()->Find(Narrow(name)) != nullptr;
    } catch (const Error&) {
        return false;
    }
}

void Extract(const wstring& name, const wstring& path) {
    std::lock_guard<std::mutex> guard(g_lock);
    const ZipReader::Entry* entry = Archive()->Find(Narrow(name));
    if (!entry) Fail(ErrorKind::NotFound, L"This Setup is missing its embedded " + name + L".");
    CreateDirs(DirName(path));
    Archive()->ExtractTo(*entry, path, true);
}

size_t ExtractTree(const wstring& prefix, const wstring& folder, const CancelToken& cancel) {
    std::lock_guard<std::mutex> guard(g_lock);
    const std::string start = Narrow(prefix);
    size_t files = 0;
    CreateDirs(folder);
    for (const auto& entry : Archive()->Entries()) {
        if (entry.directory || entry.name.compare(0, start.size(), start) != 0) continue;
        cancel.ThrowIfCanceled();
        Archive()->ExtractTo(entry, SafePath(folder, Widen(entry.name.substr(start.size()))), false);
        files++;
    }
    return files;
}
}  // namespace payload

// ---- versions ----------------------------------------------------------------------------------------------------

static std::vector<long long> VersionKey(const wstring& version) {
    std::vector<long long> key(5, 0);
    if (version.empty()) return key;
    size_t dash = version.find(L'-');
    auto numbers = Split(dash == wstring::npos ? version : version.substr(0, dash), L'.');
    for (size_t i = 0; i < 3 && i < numbers.size(); i++) ParseInt(numbers[i], key[i]);
    key[3] = dash == wstring::npos ? 1 : 0;
    if (dash != wstring::npos) {
        wstring rest = version.substr(dash + 1);
        size_t dot = rest.rfind(L'.');
        if (dot != wstring::npos) ParseInt(rest.substr(dot + 1), key[4]);
    }
    return key;
}

int CompareVersions(const wstring& a, const wstring& b) {
    auto x = VersionKey(a), y = VersionKey(b);
    for (size_t i = 0; i < x.size(); i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

bool IsBetaVersion(const wstring& version) { return IFind(version, L"-beta.") != wstring::npos; }

InstallMode ForPackage(const std::optional<wstring>& installedVersion, const wstring& packageVersion) {
    if (!installedVersion) return InstallMode::Install;
    return CompareVersions(packageVersion, *installedVersion) > 0 ? InstallMode::Update : InstallMode::Repair;
}

// ---- the installed version ---------------------------------------------------------------------------------------

std::optional<InstalledInfo> InstalledInfo::Read(const InstallLayout& layout) {
    if (!FileExists(layout.Current())) return std::nullopt;
    try {
        Json current = LoadJson(layout.Current());
        InstalledInfo info;
        info.Version = JStr(&current, L"version").value_or(L"");
        info.Previous = JStr(&current, L"previous");
        if (info.Version.empty()) return std::nullopt;
        wstring installJson = PathCombine(layout.VersionDir(info.Version), L"install.json");
        if (!FileExists(installJson)) return std::nullopt;
        LoadJson(installJson);
        if (info.Previous && (info.Previous->empty() || !DirExists(layout.VersionDir(*info.Previous)))) info.Previous.reset();
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const std::vector<wstring>& RunningPrograms() {
    static const std::vector<wstring> names = {L"Nebula",       L"Nebula-x86-64-v2", L"NebulaVk",       L"NebulaVk-x86-64-v2",
                                               L"NebulaD11",    L"NebulaD11-x86-64-v2", L"NebulaNative", L"NebulaRuntime",
                                               L"NebulaLauncher", L"NebulaSettings", L"nebula-import"};
    return names;
}

// ---- system checks -----------------------------------------------------------------------------------------------

namespace {

// The nearest folder of `path` that exists (an install folder is made later).
wstring ExistingAncestor(wstring path) {
    path = FullPath(path);
    while (!path.empty() && !DirExists(path)) {
        wstring parent = DirName(path);
        if (parent == path) break;
        path = parent;
    }
    return path;
}

long long FreeBytes(const wstring& path) {
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(ExistingAncestor(path).c_str(), &available, nullptr, nullptr)) FailWin32(GetLastError(), path);
    return static_cast<long long>(available.QuadPart);
}

wstring FileSystemName(const wstring& path) {
    wchar_t volume[MAX_PATH] = {}, name[MAX_PATH] = {};
    if (!GetVolumePathNameW(ExistingAncestor(path).c_str(), volume, MAX_PATH)) FailWin32(GetLastError(), path);
    if (!GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, name, MAX_PATH)) FailWin32(GetLastError(), volume);
    return name;
}

}  // namespace

std::vector<wstring> InstallEngine::CheckRequirements(const wstring& installRoot) {
    std::vector<wstring> problems;
    if (!IsWindows10OrGreater()) problems.push_back(Of(L"Nebula needs Windows 10 or 11."));
    // The lowest CPU level the package is built for, x86-64-v2 (SSE4.2): without it the game would stop at its first
    // such instruction, not here.
    if (!IsProcessorFeaturePresent(PF_SSE4_2_INSTRUCTIONS_AVAILABLE)) problems.push_back(Of(L"Nebula requires a CPU with SSE4.2."));
    try {
        wstring fs = FileSystemName(installRoot);
        if (!IEquals(fs, L"NTFS")) problems.push_back(Of(L"The install folder must be on an NTFS drive. This drive uses {0}.", {fs}));
        long long free = FreeBytes(installRoot);
        if (free < kRequiredFreeBytes) problems.push_back(Of(L"Setup needs {0} of free space. The drive has {1}.", {FormatBytes(kRequiredFreeBytes), FormatBytes(free)}));
    } catch (const std::exception& error) {
        problems.push_back(Of(L"Could not read the install folder: {0}", {ErrorText(error)}));
    }
    return problems;
}

// ---- content -----------------------------------------------------------------------------------------------------

long long InstallEngine::ContentFormatOf(const wstring& content) {
    wstring marker = PathCombine(content, L"content.json");
    if (!FileExists(marker) || !FileExists(PathCombine(content, L"game.pak"))) return -1;
    try {
        Json json = LoadJson(marker);
        return JLong(&json, L"formatVersion");
    } catch (const std::exception&) {
        return -1;
    }
}

long long InstallEngine::VersionContentFormat(const wstring& versionDir) {
    try {
        Json json = LoadJson(PathCombine(versionDir, L"install.json"));
        return JLong(&json, L"contentFormatVersion");
    } catch (const std::exception&) {
        return -1;
    }
}

bool InstallEngine::HoldsRecompiledGame(const wstring& dir) {
    if (FileExists(PathCombine(dir, L"NebulaRuntime.exe"))) return true;
    for (const wstring& file : ListFiles(dir, false))
        if (StartsWith(FileName(file), L"RMGE01_", true) && EndsWith(file, L".dll", true)) return true;
    return false;
}

namespace {

using Engine = InstallEngine;

// game.pak starts with "NBSTORE\0" and this build's store version.
bool CurrentStoreValid(const wstring& content) {
    HANDLE file = CreateFileW(PathCombine(content, L"game.pak").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    uint8_t header[12] = {};
    DWORD read = 0;
    bool ok = ReadFile(file, header, sizeof(header), &read, nullptr) && read == sizeof(header);
    CloseHandle(file);
    if (!ok || memcmp(header, "NBSTORE\0", 8) != 0) return false;
    uint32_t version = uint32_t(header[8]) | uint32_t(header[9]) << 8 | uint32_t(header[10]) << 16 | uint32_t(header[11]) << 24;
    return version == build::kStoreVersion;
}

bool ContentValid(const InstallLayout& layout) {
    return Engine::ContentFormatOf(layout.Content()) == Engine::kContentFormatVersion && CurrentStoreValid(layout.Content());
}

// A packed game that opens: its marker says this format, its game.pak this build's store, and it is not an empty shell.
bool StoreOpens(const wstring& content) {
    try {
        return Engine::ContentFormatOf(content) == Engine::kContentFormatVersion && CurrentStoreValid(content) &&
               FileSize(PathCombine(content, L"game.pak")) > (1LL << 20);
    } catch (const std::exception&) {
        return false;
    }
}

// This version's content, set aside by a rollback; none when there is none.
std::optional<wstring> KeptCurrentContent(const InstallLayout& layout) {
    wstring kept = layout.KeptContent(Engine::kContentFormatVersion);
    if (Engine::ContentFormatOf(kept) == Engine::kContentFormatVersion && CurrentStoreValid(kept)) return kept;
    return std::nullopt;
}

// The format-1 content to cook from: the installed one, or the one an earlier cook kept. None: the disc is needed.
std::optional<wstring> CookSource(const InstallLayout& layout) {
    for (const wstring& content : {layout.Content(), layout.KeptContent(Engine::kCookableContentFormatVersion)})
        if (Engine::ContentFormatOf(content) == Engine::kCookableContentFormatVersion) return content;
    return std::nullopt;
}

wstring GameSourcePath(const InstallLayout& layout) { return PathCombine(layout.root, L"game-source.json"); }

// The disc image or folder the packed game was last made from, when it is still there as it was (a file of the same
// size). Kept in the install root beside current.json: it belongs to this install.
std::optional<wstring> RememberedSource(const InstallLayout& layout) {
    try {
        wstring file = GameSourcePath(layout);
        if (!FileExists(file)) return std::nullopt;
        Json map = LoadJson(file);
        wstring path = JStr(&map, L"path").value_or(L"");
        if (path.empty()) return std::nullopt;
        if (JBool(&map, L"folder")) return DirExists(path) ? std::optional<wstring>(path) : std::nullopt;
        return FileExists(path) && FileSize(path) == JLong(&map, L"size") ? std::optional<wstring>(path) : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

bool InstallEngine::RetainedGameValid(const InstallLayout& layout) {
    return ContentValid(layout) || KeptCurrentContent(layout) || CookSource(layout) || RememberedSource(layout);
}

// ---- the saves and settings, copied aside before an update -------------------------------------------------------

// The player's saves, settings and the mods' small files, copied aside once before an update replaces the version they
// were last used with (a new version may convert a save in place the first time it reads it). Nothing moves: the data
// stays in the data folder, which no install, update or rollback touches. One copy per version; the newest two stay.
namespace snapshot {
namespace {
constexpr long long kSmallFile = 1LL << 20;
constexpr long long kModsBudget = 64LL << 20;
constexpr size_t kKeptCopies = 2;

wstring Folder() { return PathCombine(paths::DataRoot(), L"update-backup"); }

wstring SafeName(wstring text) {
    for (wchar_t& c : text)
        if (wcschr(L"<>:\"/\\|?*", c) || c < 32) c = L'_';
    return text;
}

int CopyModsFiles(const wstring& mods, const wstring& target, const std::function<void(const wstring&)>& log) {
    if (!DirExists(mods)) return 0;
    int copied = 0, skipped = 0;
    long long left = kModsBudget;
    for (const wstring& file : ListFiles(mods, true)) {
        long long length = FileSize(file);
        if (length > kSmallFile || length > left) {
            skipped++;
            continue;
        }
        left -= length;
        try {
            wstring destination = PathCombine(target, TrimStart(file.substr(mods.size()), L"\\"));
            CreateDirs(DirName(destination));
            CopyFileOrThrow(file, destination, false);
            copied++;
        } catch (const Error&) {
            skipped++;
        }
    }
    if (skipped != 0) log(Num(skipped) + L" larger files of the mods were left where they are.");
    return copied;
}

void Prune(const wstring& keep, const std::function<void(const wstring&)>& log) {
    struct Copy {
        wstring path;
        FILETIME created;
    };
    std::vector<Copy> copies;
    for (const wstring& dir : ListDirs(Folder(), false)) {
        if (EndsWith(dir, L".partial")) continue;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(dir.c_str(), GetFileExInfoStandard, &data)) copies.push_back({dir, data.ftCreationTime});
    }
    std::sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) { return CompareFileTime(&a.created, &b.created) > 0; });
    for (size_t i = kKeptCopies; i < copies.size(); i++) {
        if (IEquals(FullPath(copies[i].path), FullPath(keep))) continue;
        try {
            DeleteTree(copies[i].path);
            log(L"Removed the older copy " + copies[i].path);
        } catch (const std::exception& error) {
            log(L"Could not remove " + copies[i].path + L": " + ErrorText(error));
        }
    }
}
}  // namespace

void Take(const std::optional<wstring>& from, const std::function<void(const wstring&)>& log) {
    wstring data = paths::DataRoot();
    if (!from || from->empty() || !DirExists(data)) return;
    wstring target = PathCombine(Folder(), SafeName(*from));
    if (DirExists(target)) {
        log(L"The saves and settings were copied aside before: " + target);
        return;
    }
    wstring partial = target + L".partial";
    try {
        if (DirExists(partial)) DeleteTree(partial);
        CreateDirs(partial);
        int copied = 0;
        for (const wchar_t* name : {L"saves", L"settings", L"nand"}) {
            wstring source = PathCombine(data, name);
            if (!DirExists(source)) continue;
            CopyTreeWithStreams(source, PathCombine(partial, name), nullptr);
            copied++;
        }
        for (const wstring& file : ListFiles(data, false)) {
            try {
                if (FileSize(file) > kSmallFile) continue;
                CopyFileOrThrow(file, PathCombine(partial, FileName(file)), false);
                copied++;
            } catch (const Error&) {
                log(L"Left out " + file + L": it is in use.");
            }
        }
        copied += CopyModsFiles(PathCombine(data, L"mods"), PathCombine(partial, L"mods"), log);
        if (copied == 0) {
            DeleteTree(partial);
            return;
        }
        MoveDirectoryInPlace(partial, target);
        log(L"Copied the saves, settings and the mods' small files to " + target + L".");
        Prune(target, log);
    } catch (const std::exception& error) {
        log(L"Could not copy the saves and settings aside (the update goes on; they are not touched): " + ErrorText(error));
        try {
            if (DirExists(partial)) DeleteTree(partial);
        } catch (const std::exception&) {
        }
    }
}
}  // namespace snapshot

// ---- the engine --------------------------------------------------------------------------------------------------

namespace {

// What a version folder holds (each from the package).
const wchar_t* const kNativeFiles[] = {
    L"Nebula.exe", L"Nebula-x86-64-v2.exe", L"NebulaVk.exe", L"NebulaVk-x86-64-v2.exe", L"NebulaD11.exe",
    L"NebulaD11-x86-64-v2.exe", L"smg_game.dll", L"smg_game_plain.dll", L"x86-64-v2\\smg_game.dll",
    L"x86-64-v2\\smg_game_plain.dll", L"nebula_import.dll", L"nebula-import.exe", L"dxc\\dxc.exe",
    L"dxc\\dxcompiler.dll", L"dxc\\dxil.dll", L"dxc\\LICENSE-MS.txt", L"dxc\\LICENCE-MIT.txt", L"dxc\\LICENSE-LLVM.txt",
    L"glslang\\glslang.exe", L"glslang\\LICENSE.txt", L"D3D12\\D3D12Core.dll", L"D3D12\\LICENSE.txt", L"LICENSE",
    L"THIRD-PARTY-NOTICES.md", L"README.md"};

// The lowest CPU level the package's programs and game modules are built for (Nebula.exe starts the x86-64-v2 program
// where the CPU lacks AVX2).
const wchar_t* const kNativeIsa = L"x86-64-v2";

void Swap(const wstring& fresh, const wstring& destination, const wstring& aside, const std::function<void(const wstring&)>& log) {
    // What is there is renamed aside first, never copied (a copy would then delete it file by file, and stop half way
    // at a file another program holds), and renamed back if the new tree cannot move in.
    bool existed = DirExists(destination);
    if (existed) MoveDirectoryInPlace(destination, aside);
    try {
        MoveDirectory(fresh, destination, log);
    } catch (const std::exception&) {
        if (existed) {
            try {
                MoveDirectoryInPlace(aside, destination);
            } catch (const std::exception& error) {
                log(L"Could not put " + destination + L" back from " + aside + L": " + ErrorText(error));
            }
        }
        throw;
    }
}

}  // namespace

struct InstallEngine::Impl {
    InstallRequest request;
    Progress& progress;
    CancelToken& cancel;
    InstallLayout layout;
    HANDLE logFile = INVALID_HANDLE_VALUE;
    std::mutex logLock;
    ULONGLONG started = GetTickCount64();
    wstring staging;
    double stageBase = 0, stageWeight = 0;
    wstring stepText;
    InstallEngine& owner;

    Impl(InstallRequest r, Progress& p, CancelToken& c, InstallEngine& o)
        : request(std::move(r)), progress(p), cancel(c), layout(request.InstallRoot), owner(o) {
        // A portable copy is marked first: its log and the copy of its saves go to its own data folder.
        if (request.Portable || paths::IsPortableRoot(layout.root)) {
            CreateDirs(layout.root);
            wstring marker = PathCombine(layout.root, paths::kPortableMarker);
            if (!FileExists(marker))
                WriteAllText(marker, L"Nebula keeps its saves and settings in the data folder here. Keep this folder together: copy or move it whole.\r\n");
            paths::UsePortableRoot(layout.root);
        }
        CreateDirs(paths::SetupLogs());
        owner.LogPath = PathCombine(paths::SetupLogs(), UtcStamp(true) + L"-" + NewGuidN() + L".log");
        logFile = CreateFileW(owner.LogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logFile == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), owner.LogPath);
    }

    ~Impl() {
        if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
    }

    void Log(const wstring& line) {
        {
            std::lock_guard<std::mutex> guard(logLock);
            std::string bytes = Narrow(UtcClock() + L" " + line + L"\r\n");
            DWORD written = 0;
            WriteFile(logFile, bytes.data(), DWORD(bytes.size()), &written, nullptr);
        }
        progress.Log(line);
    }

    void Stage(double start, double weight, const wstring& text) {
        cancel.ThrowIfCanceled();
        stageBase = start;
        stageWeight = weight;
        stepText = text;
        Log(L"== " + text);
        progress.Report(start, text, -1);
    }

    void StepProgress(const std::optional<wstring>& text, double fraction) {
        if (text) stepText = *text;
        progress.Report(stageBase + stageWeight * std::max(0.0, std::min(1.0, fraction)), stepText, fraction);
    }

    void Run() {
        Log(L"Nebula Setup " + wstring(build::kVersion) + L" (" + build::kCommit + L"), " +
            (request.Mode == InstallMode::Install ? L"Install" : request.Mode == InstallMode::Update ? L"Update" : L"Repair"));
        Log(L"Install folder: " + layout.root);
        CreateDirs(layout.root);
        CreateDirs(layout.Staging());
        CleanAbandonedStaging();
        staging = PathCombine(layout.Staging(), UtcStamp(false) + L"-" + NewGuidN().substr(0, 8));
        CreateDirs(staging);
        try {
            Install(InstalledInfo::Read(layout));
        } catch (const std::exception& error) {
            Log(L"FAILED: " + ErrorText(error));
            TryDeleteTree(staging);
            throw;
        }
        TryDeleteTree(staging);
    }

    void CleanAbandonedStaging() {
        for (const wstring& dir : ListDirs(layout.Staging(), false)) {
            try {
                DeleteTree(dir);
                Log(L"Removed unfinished work: " + dir);
            } catch (const std::exception&) {
                // the Setup running from it (an update), or a file still held: a later run removes it
            }
        }
    }

    void TryDeleteTree(const wstring& dir) {
        if (!DirExists(dir)) return;
        try {
            DeleteTree(dir);
            Log(L"Removed " + dir);
        } catch (const std::exception& error) {
            Log(L"Could not remove " + dir + L": " + ErrorText(error));
        }
    }

    wstring ImporterPath() {
        wstring path = PathCombine(staging, L"nebula-import.exe");
        if (!FileExists(path)) payload::Extract(L"app/nebula-import.exe", path);
        return path;
    }

    int RunTool(const std::vector<wstring>& arguments, const std::function<void(const wstring&)>& onLine) {
        wstring importer = ImporterPath();
        Log(L"> nebula-import.exe " + JoinArgs(arguments));
        ProcessRunner runner;
        return runner.Run(importer, arguments, staging, nullptr, onLine, cancel);
    }

    // The importer's output: PROGRESS done total, STEP n text, and log lines.
    std::function<void(const wstring&)> ImportLines() {
        return [this](const wstring& line) {
            if (StartsWith(line, L"PROGRESS ")) {
                auto parts = Split(line, L' ');
                long long done = 0, total = 0;
                if (parts.size() == 3 && ParseInt(parts[1], done) && ParseInt(parts[2], total)) {
                    StepProgress(std::nullopt, double(done) / double(std::max(1LL, total)));
                    return;
                }
            }
            if (StartsWith(line, L"STEP ")) {
                size_t space = line.find(L' ', 5);
                if (space != wstring::npos) StepProgress(line.substr(space + 1), -1);
            }
            Log(line);
        };
    }

    void Identify(const wstring& input) {
        std::vector<wstring> lines;
        int code = RunTool({L"identify", input}, [&](const wstring& line) { lines.push_back(line); });
        wstring json = Join(lines, L"\n");
        Log(json);
        Json identity;
        try {
            size_t brace = json.find(L'{');
            identity = ParseJson(brace == wstring::npos ? json : json.substr(brace));
        } catch (const std::exception&) {
            Fail(ErrorKind::InvalidOperation, !lines.empty() ? lines.back() : Of(L"The selected file could not be read."));
        }
        if (code != 0 || !JBool(&identity, L"supported")) Fail(ErrorKind::InvalidOperation, JStr(&identity, L"message").value_or(Of(L"The selected file could not be read.")));
    }

    void Import(const wstring& input, const wstring& content) {
        int code = RunTool({L"import", input, L"--output", content}, ImportLines());
        if (code != 0) Fail(ErrorKind::InvalidOperation, Of(L"Could not read the game (error {0}). See the Setup log.", {Num(code)}));
    }

    void Cook(const wstring& content, const wstring& output) {
        int code = RunTool({L"cook", content, L"--output", output}, ImportLines());
        if (code != 0) Fail(ErrorKind::InvalidOperation, Of(L"Could not pack the game (error {0}). See the Setup log.", {Num(code)}));
    }

    void RememberSource(const wstring& input) {
        try {
            bool folder = DirExists(input);
            Json map = Json::Object();
            map.Set(L"path", Json::Str(FullPath(input)));
            map.Set(L"folder", Json::Bool(folder));
            map.Set(L"size", Json::Int(folder ? 0 : FileSize(input)));
            map.Set(L"storeVersion", Json::Int(build::kStoreVersion));
            map.Set(L"recordedUtc", Json::Str(UtcNowIso()));
            SaveJson(GameSourcePath(layout), map);
        } catch (const std::exception& error) {
            Log(L"Could not note where the game came from: " + ErrorText(error));
        }
    }

    // Asks until nothing of Nebula runs from `folder`; unattended (or on Cancel) it fails.
    void WaitUntilClosed(const wstring& folder) {
        while (auto running = RunningFrom(folder, RunningPrograms())) {
            Log(L"Waiting for " + *running + L" to close.");
            if (!progress.AskToClose(Of(L"Nebula ({0}) is running. Close it, then choose Retry to install Nebula {1}.", {*running, build::kVersion})))
                Fail(ErrorKind::InvalidOperation, Of(L"Close Nebula, then try again."));
        }
    }

    void Install(const std::optional<InstalledInfo>& current) {
        const bool reuseContent = !request.GameInput && ContentValid(layout);
        const std::optional<wstring> keptContent = !request.GameInput && !reuseContent ? KeptCurrentContent(layout) : std::nullopt;
        const std::optional<wstring> cookSource = !request.GameInput && !reuseContent && !keptContent ? CookSource(layout) : std::nullopt;
        // A packed game of an older store cannot be read by this version nor cooked again: it is packed again from the
        // image it was made from, when that is still where it was, so an update needs no question.
        const std::optional<wstring> remembered =
            !request.GameInput && !reuseContent && !keptContent && !cookSource ? RememberedSource(layout) : std::nullopt;
        const std::optional<wstring> gameInput = request.GameInput ? request.GameInput : remembered;
        if (!reuseContent && !keptContent && !cookSource && !gameInput)
            Fail(ErrorKind::InvalidOperation, Of(L"Choose a Super Mario Galaxy disc image or folder."));
        Log(L"Reuse the packed game: " + wstring(reuseContent ? L"yes" : L"no") + L"; kept for this version: " + keptContent.value_or(L"-") +
            L"; cook from: " + cookSource.value_or(L"-") + L"; pack again from: " + remembered.value_or(L"-"));
        bool imported = false;
        if (cookSource) {
            Stage(0.00, 0.70, Of(L"Converting the game files"));
            Cook(*cookSource, PathCombine(staging, L"content"));
        } else if (!reuseContent && !keptContent) {
            Stage(0.00, 0.02, Of(L"Checking the game"));
            Identify(*gameInput);
            Stage(0.02, 0.68, Of(L"Packing the game files"));
            Import(*gameInput, PathCombine(staging, L"content"));
            imported = true;
        }

        Stage(0.75, 0.15, Of(L"Installing Nebula {0}", {build::kVersion}));
        wstring app = PathCombine(staging, L"app");
        size_t files = payload::ExtractTree(L"app/", app, cancel);
        Log(L"Unpacked " + Num(static_cast<long long>(files)) + L" files.");
        for (const wchar_t* name : kNativeFiles) {
            wstring path = PathCombine(app, name);
            if (!FileExists(path) || FileSize(path) == 0) Fail(ErrorKind::InvalidData, L"The new version's " + wstring(name) + L" is missing or empty.");
        }
        WriteAllText(PathCombine(app, L"runtime-isa.txt"), kNativeIsa);
        Json hashes = Json::Object();
        for (const wstring& path : ListFiles(app, true)) hashes.Set(TrimStart(path.substr(app.size()), L"\\"), Json::Str(Sha256File(path)));
        Json install = Json::Object();
        install.Set(L"schema", Json::Str(L"nebula.install.v1"));
        install.Set(L"version", Json::Str(build::kVersion));
        install.Set(L"commit", Json::Str(build::kCommit));
        install.Set(L"repository", Json::Str(build::kRepository));
        install.Set(L"game", Json::Str(L"native"));
        install.Set(L"contentFormatVersion", Json::Int(kContentFormatVersion));
        install.Set(L"runtimeIsa", Json::Str(kNativeIsa));
        install.Set(L"installedUtc", Json::Str(UtcNowIso()));
        install.Set(L"files", hashes);
        SaveJson(PathCombine(app, L"install.json"), install);

        Stage(0.94, 0.06, Of(L"Switching to the new version"));
        // The versions it replaces are removed: nothing may run from this install meanwhile.
        WaitUntilClosed(layout.root);
        std::optional<wstring> previous = Commit(app, current, keptContent);
        RemoveOtherVersions(previous);
        if (imported) RememberSource(*gameInput);
        RemoveRecompiledGame();
        auto installed = InstalledInfo::Read(layout);
        PruneKeptContent(installed ? installed->Previous : std::nullopt);
        progress.Report(1.0, Of(L"Nebula {0} is installed.", {build::kVersion}), 1);
        ULONGLONG seconds = (GetTickCount64() - started) / 1000;
        Log(L"Completed in " + Num(static_cast<long long>(seconds / 60)) + L" min " + Num(static_cast<long long>(seconds % 60)) + L" s.");
    }

    // Switches to the new version and returns the one kept for rollback. Nothing old is deleted here.
    std::optional<wstring> Commit(const wstring& app, const std::optional<InstalledInfo>& current, const std::optional<wstring>& keptContent) {
        const wstring versionDir = layout.VersionDir(build::kVersion);
        const wstring newContent = keptContent.value_or(PathCombine(staging, L"content"));
        const bool swapContent = DirExists(newContent);
        if (swapContent && !keptContent) {
            // The new packed game is stamped and has to open before anything installed is touched.
            Json marker = Json::Object();
            marker.Set(L"formatVersion", Json::Int(kContentFormatVersion));
            marker.Set(L"createdUtc", Json::Str(UtcNowIso()));
            SaveJson(PathCombine(newContent, L"content.json"), marker);
            if (!StoreOpens(newContent)) Fail(ErrorKind::InvalidData, Of(L"The game files could not be verified. Nothing was changed."));
        }
        CreateDirs(layout.Versions());
        // The saves, the settings and the mods' small files stay where they are; a copy is taken once, before a new
        // version can convert anything of them.
        snapshot::Take(current ? std::optional<wstring>(current->Version) : std::nullopt, [this](const wstring& line) { Log(line); });
        std::optional<wstring> previous;
        if (current) previous = current->Version != build::kVersion ? std::optional<wstring>(current->Version) : current->Previous;
        const wstring replaced = versionDir + L".replaced-" + NewGuidN().substr(0, 8);
        const wstring oldContent = PathCombine(staging, L"content-old");
        Swap(app, versionDir, replaced, [this](const wstring& line) { Log(line); });
        bool contentSwapped = false;
        try {
            if (swapContent) {
                Swap(newContent, layout.Content(), oldContent, [this](const wstring& line) { Log(line); });
                contentSwapped = true;
            }
            if (!FileExists(PathCombine(versionDir, L"Nebula.exe")) || !StoreOpens(layout.Content()))
                Fail(ErrorKind::InvalidData, Of(L"The update could not be verified. The previous version was restored."));
            Json record = Json::Object();
            record.Set(L"version", Json::Str(build::kVersion));
            record.Set(L"previous", previous ? Json::Str(*previous) : Json::Null());
            SaveJson(layout.Current(), record);
        } catch (const std::exception& error) {
            Log(L"Putting the previous version back: " + ErrorText(error));
            UndoCommit(versionDir, replaced, contentSwapped, newContent, oldContent);
            throw;
        }
        if (DirExists(replaced)) TryDeleteTree(replaced);
        if (swapContent) KeepReplacedContent(oldContent);
        // Nebula is installed from here on: what follows does not fail the install.
        PlaceStarter();
        wstring self = FullPath(ModulePath());
        if (!IEquals(self, FullPath(layout.SetupCopy()))) CopySetup(self);
        for (const wstring& failure : integration::Register(layout, build::kVersion, request.DesktopShortcut && !paths::IsPortableRoot(layout.root)))
            Log(L"Could not create the shortcut " + failure + L"; Nebula.exe is in " + layout.root + L".");
        owner.InstalledVersionDir = versionDir;
        Log(L"Installed " + wstring(build::kVersion) + L" to " + versionDir + (previous ? L"; kept " + *previous + L" for rollback." : L"."));
        return previous;
    }

    void UndoCommit(const wstring& versionDir, const wstring& replaced, bool contentSwapped, const wstring& newContent, const wstring& oldContent) {
        if (contentSwapped) {
            try {
                MoveDirectoryInPlace(layout.Content(), newContent);
                if (DirExists(oldContent)) MoveDirectoryInPlace(oldContent, layout.Content());
            } catch (const std::exception& error) {
                Log(L"Could not put the previous game files back: " + ErrorText(error));
            }
        }
        try {
            DeleteTree(versionDir);
            if (DirExists(replaced)) MoveDirectoryInPlace(replaced, versionDir);
        } catch (const std::exception& error) {
            Log(L"Could not put the previous version back: " + ErrorText(error));
        }
    }

    // The content an update replaced, by its format, beside the new.
    void KeepReplacedContent(const wstring& old) {
        long long format = ContentFormatOf(old);
        if (format <= 0 || format == kContentFormatVersion) return;
        wstring kept = layout.KeptContent(format);
        try {
            if (DirExists(kept)) DeleteTree(kept);
            MoveDirectoryInPlace(old, kept);
            Log(L"Kept the previous content for rollback in " + kept);
        } catch (const std::exception& error) {
            Log(L"Could not keep " + old + L": " + ErrorText(error));
        }
    }

    // Of the kept content, format 1 (cooked from) and the format the previous version reads stay.
    void PruneKeptContent(const std::optional<wstring>& previous) {
        long long reads = previous ? VersionContentFormat(layout.VersionDir(*previous)) : -1;
        for (const wstring& dir : ListDirs(layout.root, false)) {
            wstring name = FileName(dir);
            if (!StartsWith(name, InstallLayout::kKeptContentPrefix)) continue;
            long long format = 0;
            if (!ParseInt(name.substr(wcslen(InstallLayout::kKeptContentPrefix)), format) || format == kCookableContentFormatVersion || format == reads) continue;
            TryDeleteTree(dir);
        }
    }

    // Once the new version is in place: exactly one previous version stays for rollback.
    void RemoveOtherVersions(const std::optional<wstring>& previous) {
        for (const wstring& dir : ListDirs(layout.Versions(), false)) {
            wstring name = FileName(dir);
            if (IEquals(name, build::kVersion) || (previous && IEquals(name, *previous))) continue;
            TryDeleteTree(dir);
        }
    }

    // After an install: versions that hold a recompiled game (1.x), the compiler and the source those installs
    // downloaded go. A file still held is logged and removed by a later install.
    void RemoveRecompiledGame() {
        auto current = InstalledInfo::Read(layout);
        std::optional<wstring> previous = current ? current->Previous : std::nullopt;
        bool previousRemoved = false;
        for (const wstring& dir : ListDirs(layout.Versions(), false)) {
            wstring name = FileName(dir);
            if (IEquals(name, build::kVersion) || !HoldsRecompiledGame(dir)) continue;
            TryDeleteTree(dir);
            previousRemoved = previousRemoved || (previous && IEquals(name, *previous));
        }
        TryDeleteTree(layout.Toolchains());
        TryDeleteTree(layout.Sources());
        TryDeleteTree(PathCombine(layout.root, L"game-inputs"));
        if (previousRemoved) {
            Json record = Json::Object();
            record.Set(L"version", Json::Str(build::kVersion));
            record.Set(L"previous", Json::Null());
            SaveJson(layout.Current(), record);
        }
    }

    // The install root's Nebula.exe: the Start menu's, the desktop's and a portable copy's way in.
    void PlaceStarter() {
        wstring next = layout.Starter() + L".new";
        try {
            payload::Extract(L"start/Nebula.exe", next);
            for (int attempt = 0;; attempt++) {
                if (MoveFileExW(next.c_str(), layout.Starter().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
                if (attempt >= 20) FailWin32(GetLastError(), layout.Starter());
                Sleep(250);
            }
        } catch (const std::exception& error) {
            DeleteFileQuietly(next);
            Log(L"Could not place " + layout.Starter() + L": " + ErrorText(error));
        }
    }

    // The Start menu's Update and Setup shortcuts run this copy. A held file is retried, then logged.
    void CopySetup(const wstring& self) {
        for (int attempt = 0;; attempt++) {
            try {
                CopyFileOrThrow(self, layout.SetupCopy(), true);
                return;
            } catch (const Error& error) {
                if (attempt >= 20) {
                    Log(L"Could not copy Setup to " + layout.SetupCopy() + L": " + error.message);
                    return;
                }
                Sleep(500);
            }
        }
    }
};

InstallEngine::InstallEngine(InstallRequest request, Progress& progress, CancelToken& cancel)
    : impl_(std::make_unique<Impl>(std::move(request), progress, cancel, *this)) {}
InstallEngine::~InstallEngine() = default;
void InstallEngine::Run() { impl_->Run(); }

// ---- integration -------------------------------------------------------------------------------------------------

namespace integration {
namespace {
const wchar_t* const kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Nebula";
const wchar_t* const kLumaFileType = L"Nebula.LumaMod";
const wchar_t* const kLinkScheme = L"nebula";
const wchar_t* const kRemoverPrefix = L"Nebula-Uninstall-";

wstring StartMenuFolder() { return PathCombine(KnownFolder(FOLDERID_Programs), L"Nebula"); }
wstring DesktopLink() { return PathCombine(KnownFolder(FOLDERID_Desktop), L"Nebula.lnk"); }

void Shortcut(const wstring& path, const wstring& target, const wstring& arguments, const wstring& workingDirectory, const wstring& description) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr)) Fail(ErrorKind::InvalidOperation, L"Shortcuts are not available (" + Num(static_cast<long long>(hr)) + L").");
    link->SetPath(target.c_str());
    link->SetArguments(arguments.c_str());
    link->SetWorkingDirectory(workingDirectory.c_str());
    link->SetDescription(description.c_str());
    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(hr)) {
        hr = file->Save(path.c_str(), TRUE);
        file->Release();
    }
    link->Release();
    if (FAILED(hr)) Fail(ErrorKind::Io, L"The shortcut could not be saved (" + Num(static_cast<long long>(hr)) + L").");
}

// A key of HKEY_CURRENT_USER, created when missing.
struct Key {
    HKEY h = nullptr;
    explicit Key(const wstring& path) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) h = nullptr;
    }
    ~Key() {
        if (h) RegCloseKey(h);
    }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    void Set(const wchar_t* name, const wstring& value) {
        if (h) RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
    }
    void SetNone(const wchar_t* name) {
        if (h) RegSetValueExW(h, name, 0, REG_NONE, nullptr, 0);
    }
    void SetDword(const wchar_t* name, DWORD value) {
        if (h) RegSetValueExW(h, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }
    void Delete(const wchar_t* name) {
        if (h) RegDeleteValueW(h, name);
    }
};

void DeleteTreeKey(const wstring& path) {
    RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
}

// Nebula's own entries of HKCU\Software\Classes\.luma (the default, when it is still Nebula's, and its Open with
// entry) go; the rest, such as LumaLink's, stays. The key goes only once it is empty.
void RemoveLumaEntries() {
    const wstring extension = L"Software\\Classes\\.luma";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, extension.c_str(), 0, KEY_READ | KEY_WRITE, &key) != ERROR_SUCCESS) return;
    wchar_t value[256] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS && wcscmp(value, kLumaFileType) == 0)
        RegDeleteValueW(key, nullptr);
    HKEY openWith = nullptr;
    if (RegOpenKeyExW(key, L"OpenWithProgids", 0, KEY_READ | KEY_WRITE, &openWith) == ERROR_SUCCESS) {
        RegDeleteValueW(openWith, kLumaFileType);
        DWORD values = 0, keys = 0;
        RegQueryInfoKeyW(openWith, nullptr, nullptr, nullptr, &keys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(openWith);
        if (values == 0 && keys == 0) RegDeleteKeyW(key, L"OpenWithProgids");
    }
    DWORD values = 0, keys = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &keys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    if (values == 0 && keys == 0) RegDeleteKeyW(HKEY_CURRENT_USER, extension.c_str());
}

void DeleteQuietly(const wstring& path) {
    try {
        if (FileExists(path)) DeleteFileOrThrow(path);
    } catch (const std::exception&) {
    }
}
}  // namespace

std::vector<wstring> Register(const InstallLayout& layout, const wstring& version, bool desktop) {
    std::vector<wstring> failures;
    // A portable copy changes nothing outside its folder; its Nebula.exe is beside its Setup. A run whose
    // %LOCALAPPDATA% is another folder than the user's (sandboxed, or a test) leaves the user's shortcuts and registry.
    if (paths::IsPortableRoot(layout.root) || paths::LocalAppDataRedirected()) return failures;
    const wstring starter = layout.Starter();
    const wstring icon = PathCombine(layout.VersionDir(version), L"Nebula.exe") + L",0";
    auto link = [&](const wstring& path, const wstring& target, const wstring& arguments, const wstring& directory, const wstring& description) {
        try {
            Shortcut(path, target, arguments, directory, description);
        } catch (const std::exception& error) {
            failures.push_back(FileName(path) + L": " + ErrorText(error));
        }
    };
    CreateDirs(StartMenuFolder());
    link(PathCombine(StartMenuFolder(), L"Nebula.lnk"), starter, L"", layout.root, L"Play Nebula");
    link(PathCombine(StartMenuFolder(), L"Update Nebula.lnk"), layout.SetupCopy(), L"--update", layout.root, L"Check for and install Nebula updates");
    link(PathCombine(StartMenuFolder(), L"Nebula Setup.lnk"), layout.SetupCopy(), L"", layout.root, L"Repair, restore or uninstall Nebula");
    if (desktop || FileExists(DesktopLink())) link(DesktopLink(), starter, L"", layout.root, L"Play Nebula");
    // Programs earlier versions had: their shortcuts go (LumaLink has a Setup of its own).
    DeleteQuietly(PathCombine(StartMenuFolder(), L"Nebula Settings.lnk"));
    DeleteQuietly(PathCombine(StartMenuFolder(), L"LumaLink.lnk"));

    {
        Key nebula(L"Software\\Nebula");
        nebula.Set(L"InstallRoot", layout.root);
        for (const wchar_t* name : {L"LumaLinkWindow", L"LumaLinkCommand", L"LumaLinkPath"}) nebula.Delete(name);
    }
    // Double-clicking a mod's file adds it; Nebula is also listed under Open with, beside LumaLink.
    {
        Key extension(L"Software\\Classes\\.luma");
        extension.Set(nullptr, kLumaFileType);
        Key openWith(L"Software\\Classes\\.luma\\OpenWithProgids");
        openWith.SetNone(kLumaFileType);
        Key type(wstring(L"Software\\Classes\\") + kLumaFileType);
        type.Set(nullptr, L"Nebula mod");
        Key typeIcon(wstring(L"Software\\Classes\\") + kLumaFileType + L"\\DefaultIcon");
        typeIcon.Set(nullptr, icon);
        Key open(wstring(L"Software\\Classes\\") + kLumaFileType + L"\\shell\\open\\command");
        open.Set(nullptr, L"\"" + starter + L"\" --import \"%1\"");
    }
    // nebula: links: the mod they name is imported (the game asks before it downloads anything).
    {
        Key scheme(wstring(L"Software\\Classes\\") + kLinkScheme);
        scheme.Set(nullptr, L"URL:Nebula link");
        scheme.Set(L"URL Protocol", L"");
        Key schemeIcon(wstring(L"Software\\Classes\\") + kLinkScheme + L"\\DefaultIcon");
        schemeIcon.Set(nullptr, icon);
        Key open(wstring(L"Software\\Classes\\") + kLinkScheme + L"\\shell\\open\\command");
        open.Set(nullptr, L"\"" + starter + L"\" --import \"%1\"");
    }
    {
        Key uninstall(kUninstallKey);
        uninstall.Set(L"DisplayName", L"Nebula");
        uninstall.Set(L"DisplayVersion", version);
        uninstall.Set(L"Publisher", L"keepvaibin");
        uninstall.Set(L"InstallLocation", layout.root);
        uninstall.Set(L"DisplayIcon", PathCombine(layout.VersionDir(version), L"Nebula.exe"));
        uninstall.Set(L"UninstallString", L"\"" + layout.SetupCopy() + L"\" --uninstall");
        uninstall.Set(L"ModifyPath", L"\"" + layout.SetupCopy() + L"\"");
        uninstall.Set(L"URLInfoAbout", L"https://github.com/" + wstring(build::kRepository));
        uninstall.SetDword(L"NoRepair", 0);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return failures;
}

void RequireClosed(const wstring& root) {
    if (RunningFrom(root, RunningPrograms())) Fail(ErrorKind::InvalidOperation, Of(L"Close Nebula, then try again."));
}

namespace {
// The kept content of `format` in place, the content there kept beside it; if it cannot move in, the content returns.
void SwapContent(const InstallLayout& layout, long long format, long long active) {
    wstring kept = layout.KeptContent(format), aside = layout.KeptContent(active);
    bool existed = DirExists(layout.Content());
    if (existed) {
        if (DirExists(aside)) DeleteTree(aside);
        MoveDirectoryInPlace(layout.Content(), aside);
    }
    try {
        MoveDirectoryInPlace(kept, layout.Content());
    } catch (const std::exception&) {
        if (existed) MoveDirectoryInPlace(aside, layout.Content());
        throw;
    }
}
}  // namespace

wstring Rollback(const InstallLayout& layout) {
    RequireClosed(layout.root);
    auto current = InstalledInfo::Read(layout);
    if (!current || !current->Previous) Fail(ErrorKind::InvalidOperation, Of(L"No previous version is available."));
    const wstring previous = *current->Previous, versionDir = layout.VersionDir(previous);
    if (InstallEngine::HoldsRecompiledGame(versionDir))
        Fail(ErrorKind::InvalidOperation, Of(L"Nebula {0} cannot be restored. Nebula {1} remains installed.", {previous, current->Version}));
    long long format = InstallEngine::VersionContentFormat(versionDir), active = InstallEngine::ContentFormatOf(layout.Content());
    if (format <= 0)
        Fail(ErrorKind::InvalidOperation, Of(L"Nebula {0} cannot be restored. Nebula {1} remains installed.", {previous, current->Version}));
    bool swap = format != active;
    if (swap && InstallEngine::ContentFormatOf(layout.KeptContent(format)) != format)
        Fail(ErrorKind::InvalidOperation, Of(L"Nebula {0} cannot be restored. Nebula {1} remains installed.", {previous, current->Version}));
    if (swap) SwapContent(layout, format, active);
    try {
        Json record = Json::Object();
        record.Set(L"version", Json::Str(previous));
        record.Set(L"previous", Json::Str(current->Version));
        SaveJson(layout.Current(), record);
    } catch (const std::exception&) {
        if (swap) SwapContent(layout, active, format);
        throw;
    }
    Register(layout, previous, false);
    return previous;
}

void Uninstall(const InstallLayout& layout, bool removeUserData) {
    RequireClosed(layout.root);
    const bool portable = paths::IsPortableRoot(layout.root);
    std::vector<wstring> kept;
    if (portable && !removeUserData) {
        kept.push_back(FullPath(paths::PortableData(layout.root)));
        kept.push_back(FullPath(PathCombine(layout.root, paths::kPortableMarker)));
    }
    // Files first: if one is held, the Add/Remove Programs entry and shortcuts are still there to try again.
    const wstring self = FullPath(ModulePath());
    for (const wstring& entry : ListEntries(layout.root)) {
        const wstring full = FullPath(entry);
        if (IEquals(full, self) || std::any_of(kept.begin(), kept.end(), [&](const wstring& k) { return IEquals(k, full); })) continue;
        try {
            if (DirExists(entry)) DeleteTree(entry);
            else DeleteFileOrThrow(entry);
        } catch (const Error& error) {
            if (error.kind != ErrorKind::Io && error.kind != ErrorKind::Access) throw;
            Fail(ErrorKind::Io, Of(L"Could not remove {0}: {1}", {entry, error.message}) + L" " + Of(L"Close it and uninstall again."));
        }
    }
    if (!portable && !paths::LocalAppDataRedirected()) {
        if (DirExists(StartMenuFolder())) DeleteTree(StartMenuFolder());
        DeleteQuietly(DesktopLink());
        DeleteTreeKey(kUninstallKey);
        DeleteTreeKey(L"Software\\Nebula");
        DeleteTreeKey(wstring(L"Software\\Classes\\") + kLumaFileType);
        RemoveLumaEntries();
        DeleteTreeKey(wstring(L"Software\\Classes\\") + kLinkScheme);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    if (removeUserData && !portable && DirExists(paths::DataRoot())) DeleteTree(paths::DataRoot());
    // A running program cannot delete its own file. When this is the Setup copy in the install folder, a copy of it in
    // the temporary folder removes it and the emptied folder once this process has ended, as NSIS and Inno Setup
    // uninstallers do; that copy is removed by the next uninstall or by Windows.
    if (StartsWith(self, TrimEnd(FullPath(layout.root), L"\\") + L"\\", true)) {
        wstring folder = TempDirectory() + kRemoverPrefix + NewGuidN();
        CreateDirs(folder);
        wstring remover = PathCombine(folder, FileName(self));
        CopyFileOrThrow(self, remover, false);
        StartProgram(remover, L"--finish-uninstall " + Num(static_cast<long long>(GetCurrentProcessId())) + L" --root " + QuoteArg(layout.root), folder);
    } else if (DirExists(layout.root) && IsDirectoryEmpty(layout.root)) {
        RemoveEmptyDirectory(layout.root);
    }
}

void FinishUninstall(DWORD setupProcess, const wstring& root) {
    try {
        HANDLE setup = OpenProcess(SYNCHRONIZE, FALSE, setupProcess);
        if (setup) {
            WaitForSingleObject(setup, 60000);
            CloseHandle(setup);
        }
        InstallLayout layout(root);
        for (int attempt = 0; attempt < 20 && FileExists(layout.SetupCopy()); attempt++) {
            try {
                DeleteFileOrThrow(layout.SetupCopy());
            } catch (const Error&) {
                Sleep(500);
            }
        }
        if (DirExists(layout.root) && IsDirectoryEmpty(layout.root)) RemoveEmptyDirectory(layout.root);
        const wstring own = DirName(FullPath(ModulePath()));
        for (const wstring& folder : ListDirs(TempDirectory(), false)) {
            if (!StartsWith(FileName(folder), kRemoverPrefix, true) || IEquals(FullPath(folder), own)) continue;
            try {
                DeleteTree(folder);
            } catch (const std::exception&) {
            }
        }
    } catch (const std::exception&) {
        // Leftovers in the temporary folder are removed by the next uninstall, or by Windows.
    }
}
}  // namespace integration

// ---- updates -----------------------------------------------------------------------------------------------------

namespace updater {
namespace {
constexpr long long kMaxSignatureBytes = 64 * 1024;
constexpr long long kMaxSetupBytes = 2LL << 30;  // a release without a size in the listing: no Nebula Setup is larger

bool IsHttps(const std::optional<wstring>& url) { return url && StartsWith(*url, L"https://", true); }

// The release version a Setup carries in its version resource (inside the signed bytes).
std::optional<wstring> SetupVersion(const wstring& file) {
    DWORD ignored = 0, size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (size == 0) return std::nullopt;
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) return std::nullopt;
    wchar_t* value = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\StringFileInfo\\040904b0\\ProductVersion", reinterpret_cast<void**>(&value), &length) || !value) return std::nullopt;
    return wstring(value);
}

std::optional<ReleaseInfo> SelectNewer(const wstring& json, const wstring& installedVersion) {
    const bool beta = IsBetaVersion(installedVersion);
    const wstring setupName = beta ? L"Nebula-Beta-Setup.exe" : L"Nebula-Setup.exe";
    std::optional<ReleaseInfo> best;
    Json releases = ParseJson(json);
    if (!releases.IsArray()) Fail(ErrorKind::InvalidData, L"The release list is not a list.");
    for (const Json& release : releases.items) {
        if (JBool(&release, L"draft")) continue;
        wstring tag = JStr(&release, L"tag_name").value_or(L"");
        wstring version = StartsWith(tag, L"v") ? tag.substr(1) : tag;
        if (IsBetaVersion(version) != beta) continue;
        if (CompareVersions(version, best ? best->Version : installedVersion) <= 0) continue;
        std::optional<wstring> setup, signature;
        long long size = 0;
        if (const Json* assets = JList(&release, L"assets")) {
            for (const Json& asset : assets->items) {
                wstring name = JStr(&asset, L"name").value_or(L"");
                if (name == setupName) {
                    setup = JStr(&asset, L"browser_download_url");
                    size = JLong(&asset, L"size");
                }
                if (name == setupName + L".sig") signature = JStr(&asset, L"browser_download_url");
            }
        }
        if (!IsHttps(setup) || !IsHttps(signature)) continue;
        ReleaseInfo info;
        info.Version = version;
        info.Tag = tag;
        info.Notes = JStr(&release, L"body").value_or(L"");
        info.SetupUrl = *setup;
        info.SignatureUrl = *signature;
        info.SetupSize = size;
        info.Prerelease = JBool(&release, L"prerelease");
        best = info;
    }
    return best;
}
}  // namespace

std::optional<ReleaseInfo> FindNewer() {
    std::string json = http::GetString(L"https://api.github.com/repos/" + wstring(build::kRepository) + L"/releases?per_page=30",
                                       L"application/vnd.github+json");
    return SelectNewer(Widen(json), build::kVersion);
}

wstring Download(const ReleaseInfo& release, const wstring& directory, const std::function<void(long long)>& onBytes, const CancelToken& cancel) {
    const wstring folder = PathCombine(directory, L"update-" + release.Version);
    const wstring exe = PathCombine(folder, L"Nebula-Setup.exe");
    const wstring sig = exe + L".sig";
    // The signature is fetched afresh each time (a release published again under its version has a new one); the Setup
    // resumes a partial download, so a check that fails downloads it once more from the start.
    for (int attempt = 0;; attempt++) {
        DeleteFileQuietly(exe);
        DeleteFileQuietly(sig);
        if (attempt > 0) DeleteFileQuietly(exe + L".partial");
        http::Fetch(release.SignatureUrl, sig, L"", kMaxSignatureBytes, nullptr, cancel);
        http::Fetch(release.SetupUrl, exe, L"", release.SetupSize > 0 ? release.SetupSize : kMaxSetupBytes, onBytes, cancel);
        bool valid = false;
        try {
            valid = VerifyRsaSha256File(exe, Base64Decode(Narrow(Trim(ReadAllText(sig)))), build::kReleasePublicKeyXml);
        } catch (const Error& error) {
            if (error.kind != ErrorKind::InvalidData) throw;
        }
        if (valid) {
            // The version is inside the signed bytes: an older signed Setup published under a newer tag (a downgrade or a
            // replay) is refused like an unsigned one.
            if (SetupVersion(exe).value_or(L"") == release.Version && CompareVersions(release.Version, build::kVersion) > 0) return exe;
            DeleteFileQuietly(exe);
            Fail(ErrorKind::InvalidData, Of(L"The download is not Nebula {0} and was deleted.", {release.Version}));
        }
        DeleteFileQuietly(exe);
        if (attempt == 1)
            Fail(ErrorKind::InvalidData, Of(L"The Nebula {0} download failed its signature check and was deleted.", {release.Version}));
    }
}

wstring PlainNotes(const wstring& markdown, size_t limit) {
    wstring out;
    bool blank = true;
    for (wstring line : Split(Replace(markdown, L"\r\n", L"\n"), L'\n')) {
        line = TrimEnd(line, L" \t");
        while (StartsWith(line, L">")) line = TrimStart(line.substr(1), L" \t");
        size_t hashes = 0;
        while (hashes < line.size() && hashes < 6 && line[hashes] == L'#') hashes++;
        if (hashes > 0 && hashes < line.size() && line[hashes] == L' ') line = TrimStart(line.substr(hashes), L" ");
        size_t indent = line.find_first_not_of(L" \t");
        if (indent != wstring::npos && indent + 1 < line.size() && (line[indent] == L'*' || line[indent] == L'+') && line[indent + 1] == L' ')
            line[indent] = L'-';
        // [text](address): the text and the address.
        for (size_t open = line.find(L'['); open != wstring::npos; open = line.find(L'[', open + 1)) {
            size_t close = line.find(L"](", open);
            size_t end = close == wstring::npos ? wstring::npos : line.find(L')', close);
            if (end == wstring::npos) break;
            wstring label = line.substr(open + 1, close - open - 1), address = line.substr(close + 2, end - close - 2);
            wstring shown = label == address ? address : label + L" (" + address + L")";
            line = line.substr(0, open) + shown + line.substr(end + 1);
        }
        line = Replace(Replace(Replace(line, L"**", L""), L"__", L""), L"`", L"");
        wstring bare = Replace(Replace(Replace(Replace(line, L" ", L""), L"-", L""), L"*", L""), L"_", L"");
        if (bare.empty() && line.size() >= 3) line.clear();  // a rule
        if (Trim(line).empty()) {
            if (!blank) out += L"\r\n";
            blank = true;
            continue;
        }
        out += line + L"\r\n";
        blank = false;
    }
    out = TrimEnd(out, L"\r\n ");
    if (out.size() <= limit) return out;
    size_t cut = out.rfind(L"\r\n", limit);
    return TrimEnd(cut != wstring::npos && cut > limit / 2 ? out.substr(0, cut) : out.substr(0, limit), L"\r\n ") + L"\r\n…";
}
}  // namespace updater

}  // namespace nebula::setup
