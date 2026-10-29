#include "setup_engine.h"

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <versionhelpers.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>
#include <thread>

namespace nebula::setup {

// ---- payload -----------------------------------------------------------------------------------------------------

namespace payload {
namespace {
std::mutex g_lock;
ZipReader* Archive() {
    static std::unique_ptr<ZipReader> archive = [] {
        HMODULE module = GetModuleHandleW(nullptr);
        HRSRC resource = FindResourceW(module, L"PAYLOAD", MAKEINTRESOURCEW(10));
        if (!resource) Fail(ErrorKind::NotFound, L"This setup carries no embedded files.");
        HGLOBAL loaded = LoadResource(module, resource);
        const void* data = LockResource(loaded);
        return ZipReader::OpenMemory(data, SizeofResource(module, resource));
    }();
    return archive.get();
}

const ZipReader::Entry& Entry(const wstring& name) {
    const ZipReader::Entry* entry = Archive()->Find(Narrow(name));
    if (!entry) Fail(ErrorKind::NotFound, L"This setup is missing its embedded " + name + L".");
    return *entry;
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

Bytes ReadBytes(const wstring& name) {
    std::lock_guard<std::mutex> guard(g_lock);
    const auto& entry = Entry(name);
    return Archive()->Read(entry, entry.size);
}

std::string ReadText(const wstring& name) {
    Bytes bytes = ReadBytes(name);
    size_t skip = bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
    return std::string(bytes.begin() + skip, bytes.end());
}

void Extract(const wstring& name, const wstring& path) {
    std::lock_guard<std::mutex> guard(g_lock);
    const auto& entry = Entry(name);
    CreateDirs(DirName(path));
    Archive()->ExtractTo(entry, path, true);
}
}  // namespace payload

static Json PayloadJson(const wstring& name) { return ParseJson(Widen(payload::ReadText(name))); }

// ---- versions ----------------------------------------------------------------------------------------------------

static std::vector<long long> VersionKey(const wstring& version) {
    std::vector<long long> key(5, 0);
    if (version.empty()) return key;
    size_t dash = version.find(L'-');
    wstring main = dash == wstring::npos ? version : version.substr(0, dash);
    auto numbers = Split(main, L'.');
    for (size_t i = 0; i < 3 && i < numbers.size(); i++) {
        long long n = 0;
        if (ParseInt(numbers[i], n)) key[i] = n;
    }
    key[3] = dash == wstring::npos ? 1 : 0;
    if (dash != wstring::npos) {
        wstring rest = version.substr(dash + 1);
        size_t dot = rest.rfind(L'.');
        long long n = 0;
        if (dot != wstring::npos && ParseInt(rest.substr(dot + 1), n)) key[4] = n;
    }
    return key;
}

// MAJOR.MINOR.PATCH[-preview.N]; a release sorts after its previews.
int CompareVersions(const wstring& a, const wstring& b) {
    auto x = VersionKey(a), y = VersionKey(b);
    for (size_t i = 0; i < x.size(); i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

// A downloaded release is its own updater: an older or equal installed version is updated or repaired.
InstallMode ForPackage(const std::optional<wstring>& installedVersion, const wstring& packageVersion) {
    if (!installedVersion) return InstallMode::Install;
    return CompareVersions(packageVersion, *installedVersion) > 0 ? InstallMode::Update : InstallMode::Repair;
}

// ---- installed version -------------------------------------------------------------------------------------------

std::optional<InstalledInfo> InstalledInfo::Read(const InstallLayout& layout) {
    if (!FileExists(layout.Current())) return std::nullopt;
    try {
        Json current = LoadJson(layout.Current());
        InstalledInfo info;
        auto version = JStr(&current, L"version");
        if (!version) return std::nullopt;
        info.Version = *version;
        info.Previous = JStr(&current, L"previous");
        info.Directory = layout.VersionDir(info.Version);
        wstring installJson = PathCombine(info.Directory, L"install.json");
        if (!FileExists(installJson)) return std::nullopt;
        info.Install = LoadJson(installJson);
        if (info.Previous && !DirExists(layout.VersionDir(*info.Previous))) info.Previous.reset();
        return info;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

wstring InstalledInfo::ModuleKey() const { return JStr(&Install, L"moduleKey").value_or(L""); }
wstring InstalledInfo::ModuleSourceCommit() const {
    auto value = JStr(&Install, L"moduleSourceCommit");
    return value ? *value : JStr(&Install, L"commit").value_or(L"");
}
wstring InstalledInfo::ModuleIsa() const { return JStr(&Install, L"moduleIsa").value_or(L"SSE2"); }
long long InstalledInfo::ModuleGeneration() const { return std::max(1LL, JLong(&Install, L"moduleGeneration")); }

bool InstalledInfo::FilesIntact(const std::vector<wstring>& names) const {
    const Json* files = JObj(&Install, L"files");
    for (const auto& name : names) {
        wstring path = PathCombine(Directory, name);
        if (!files || !FileExists(path) || Sha256File(path) != JStr(files, name).value_or(L"")) return false;
    }
    return true;
}

void InstalledInfo::CopyFiles(const wstring& target, const std::vector<wstring>& names) const {
    for (const auto& name : names) CopyFileOrThrow(PathCombine(Directory, name), PathCombine(target, name), true);
}

std::vector<wstring> InstalledInfo::RuntimeDllNames() const {
    std::vector<wstring> names;
    const Json* files = JObj(&Install, L"files");
    if (!files) return names;
    for (const auto& member : files->members)
        if (EndsWith(member.first, L".dll", true) && !StartsWith(member.first, L"RMGE01_", true)) names.push_back(member.first);
    return names;
}

// ---- toolchain ---------------------------------------------------------------------------------------------------

wstring SafeJoin(const wstring& root, const wstring& relative) {
    wstring full = FullPath(PathCombine(root, Slashes(relative)));
    wstring prefix = TrimEnd(FullPath(root), L"\\") + L"\\";
    if (!StartsWith(full, prefix, true)) Fail(ErrorKind::InvalidData, L"Archive entry escapes its folder: " + relative);
    return full;
}

Toolchain::Toolchain(const Json& pins, const wstring& toolchainsDirectory) : pins_(pins) {
    // The identity of the pinned toolchain, computed by the release build over toolchain.json (part of module
    // compatibility and the toolchain folder's name).
    id_ = Trim(Widen(payload::ReadText(L"toolchain-id.txt")));
    root_ = PathCombine(toolchainsDirectory, id_);
}

wstring Toolchain::LicenseUrl() const { return JStr(JObj(&pins_, L"visualStudio"), L"licenseUrl").value_or(L""); }

long long Toolchain::DownloadBytes() const {
    long long total = JLong(&pins_, L"downloadBytes");
    for (const wchar_t* name : {L"llvm", L"cmake", L"ninja"}) total += JLong(JObj(&pins_, name), L"size");
    return total;
}

bool Toolchain::IsReady() { return FileExists(Marker()) && Locate(false); }

wstring Toolchain::MsvcCompilerVersion() const {
    // Toolset 14.x.y is compiler 19.x.y; the toolchain has no cl.exe for clang-cl to read it from.
    auto parts = Split(FileName(msvcDir_), L'.');
    long long major = 0, minor = 0, build = 0;
    if (parts.size() < 3 || !ParseInt(parts[0], major) || !ParseInt(parts[1], minor) || !ParseInt(parts[2], build) || major != 14)
        Fail(ErrorKind::InvalidData, L"The toolchain's MSVC folder has no 14.x.y version: " + msvcDir_);
    return L"19." + Num(minor) + L"." + Num(build);
}

namespace {
struct Download {
    wstring Url, File, Sha256, Kind;
    long long Size = 0;
    const Json* Pin = nullptr;
};

wstring LastSorted(std::vector<wstring> dirs) {
    if (dirs.empty()) return L"";
    std::sort(dirs.begin(), dirs.end());
    return dirs.back();
}

wstring UnescapeData(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '%' && i + 2 < text.size() && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2])) {
            out += char(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return Widen(out);
}

void ExtractVsix(const wstring& vsix, const wstring& target) {
    auto archive = ZipReader::OpenFile(vsix);
    for (const auto& entry : archive->Entries()) {
        wstring name = UnescapeData(Narrow(Replace(Widen(entry.name), L"\\", L"/")));
        if (!StartsWith(name, L"Contents/", true) || EndsWith(name, L"/")) continue;
        archive->ExtractTo(entry, SafeJoin(target, name.substr(9)), true);
    }
}

// Only the pinned entries of a release zip (its top folder removed); a key ending in "/" maps a whole folder.
void ExtractSelected(const wstring& zip, const wstring& target, const Json* map) {
    size_t found = 0;
    auto archive = ZipReader::OpenFile(zip);
    for (const auto& entry : archive->Entries()) {
        wstring name = Replace(Widen(entry.name), L"\\", L"/");
        size_t slash = name.find(L'/');
        if (slash == wstring::npos || EndsWith(name, L"/")) continue;
        wstring relative = name.substr(slash + 1);
        std::optional<wstring> mapped;
        for (const auto& pair : map->members) {
            wstring to = pair.second.text;
            if (EndsWith(pair.first, L"/") ? StartsWith(relative, pair.first) : relative == pair.first) mapped = to + relative.substr(pair.first.size());
        }
        if (!mapped) continue;
        archive->ExtractTo(entry, SafeJoin(target, *mapped), true);
        found++;
    }
    if (found < map->members.size()) Fail(ErrorKind::InvalidData, FileName(zip) + L" is missing pinned files.");
}

void ExtractZipStripTop(const wstring& zip, const wstring& target) {
    auto archive = ZipReader::OpenFile(zip);
    for (const auto& entry : archive->Entries()) {
        wstring name = Replace(Widen(entry.name), L"\\", L"/");
        size_t slash = name.find(L'/');
        if (slash == wstring::npos || EndsWith(name, L"/")) continue;
        archive->ExtractTo(entry, SafeJoin(target, name.substr(slash + 1)), true);
    }
}

void ExtractZip(const wstring& zip, const wstring& target) {
    auto archive = ZipReader::OpenFile(zip);
    for (const auto& entry : archive->Entries()) {
        wstring name = Replace(Widen(entry.name), L"\\", L"/");
        if (EndsWith(name, L"/")) {
            CreateDirs(SafeJoin(target, name));
            continue;
        }
        archive->ExtractTo(entry, SafeJoin(target, name), false);
    }
}

// An administrative MSI install only unpacks files; it needs no elevation.
void InstallMsi(const wstring& msi, const wstring& target, const std::function<void(const wstring&)>& log, const CancelToken& cancel) {
    CreateDirs(target);
    ProcessRunner runner;
    wstring system = KnownFolder(FOLDERID_System);
    // msiexec parses its own command line: PROPERTY="value" quoting.
    int code = runner.RunCommandLine(PathCombine(system, L"msiexec.exe"), L"/a \"" + msi + L"\" /qn TARGETDIR=\"" + target + L"\"",
                                     DirName(msi), nullptr, log, cancel);
    if (code != 0) Fail(ErrorKind::InvalidOperation, L"Unpacking " + FileName(msi) + L" failed with code " + Num(code) + L".");
}
}  // namespace

void Toolchain::Ensure(const std::function<void(const wstring&, double)>& progress, const std::function<void(const wstring&)>& log,
                       const CancelToken& cancel) {
    if (IsReady()) {
        log(L"Using the installed toolchain " + id_ + L".");
        return;
    }
    if (DirExists(root_)) DeleteTree(root_);
    wstring downloads = PathCombine(DirName(root_), L"downloads");
    CreateDirs(downloads);

    std::vector<Download> items;
    if (const Json* payloads = JList(&pins_, L"payloads")) {
        for (const auto& item : payloads->items) {
            Download d;
            d.Url = JStr(&item, L"url").value_or(L"");
            d.Sha256 = JStr(&item, L"sha256").value_or(L"");
            d.Size = JLong(&item, L"size");
            d.Kind = JStr(&item, L"kind").value_or(L"");
            d.File = PathCombine(downloads, FileName(JStr(&item, L"fileName").value_or(L"")));
            items.push_back(d);
        }
    }
    for (const wchar_t* name : {L"llvm", L"cmake", L"ninja"}) {
        const Json* pin = JObj(&pins_, name);
        Download d;
        d.Url = JStr(pin, L"url").value_or(L"");
        d.Sha256 = JStr(pin, L"sha256").value_or(L"");
        d.Size = JLong(pin, L"size");
        d.Kind = name;
        d.File = PathCombine(downloads, wstring(name) + L"-" + JStr(pin, L"version").value_or(L"") + L".zip");
        d.Pin = pin;
        items.push_back(d);
    }

    // Six downloads at a time.
    {
        long long total = 0;
        for (const auto& item : items) total += item.Size;
        std::vector<std::atomic<long long>> received(items.size());
        std::atomic<int> next{-1};
        std::mutex failureLock;
        std::exception_ptr failure;
        std::mutex progressLock;
        std::vector<std::thread> workers;
        for (int w = 0; w < 6; w++) {
            workers.emplace_back([&] {
                try {
                    for (int index; (index = ++next) < int(items.size());) {
                        {
                            std::lock_guard<std::mutex> guard(failureLock);
                            if (failure) return;
                        }
                        const Download& item = items[size_t(index)];
                        http::Fetch(item.Url, item.File, item.Sha256, item.Size, [&, index](long long bytes) {
                            received[size_t(index)] = bytes;
                            long long sum = 0;
                            for (const auto& r : received) sum += r.load();
                            std::lock_guard<std::mutex> guard(progressLock);
                            progress(L"Downloading the compiler toolchain", double(sum) / double(std::max(1LL, total)));
                        }, cancel);
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> guard(failureLock);
                    if (!failure) failure = std::current_exception();
                }
            });
        }
        for (auto& worker : workers) worker.join();
        if (failure) std::rethrow_exception(failure);
    }
    log(L"Downloaded " + Num((long long)items.size()) + L" toolchain files.");

    wstring staging = root_ + L".partial";
    if (DirExists(staging)) DeleteTree(staging);
    CreateDirs(staging);
    progress(L"Unpacking the toolchain", -1);
    // msiexec runs one install at a time, so the SDK unpacks on its own thread while the archives extract alongside it.
    std::exception_ptr sdkFailure;
    std::thread sdk([&] {
        try {
            for (const auto& item : items) {
                if (item.Kind != L"msi") continue;
                cancel.ThrowIfCanceled();
                InstallMsi(item.File, PathCombine(staging, L"sdk"), log, cancel);
            }
        } catch (...) {
            sdkFailure = std::current_exception();
        }
    });
    try {
        for (const auto& item : items) {
            cancel.ThrowIfCanceled();
            if (item.Kind == L"vsix") ExtractVsix(item.File, staging);
            else if (item.Kind == L"llvm") ExtractSelected(item.File, PathCombine(staging, L"llvm"), JObj(item.Pin, L"extract"));
            else if (item.Kind == L"cmake") ExtractZipStripTop(item.File, PathCombine(staging, L"cmake"));
            else if (item.Kind == L"ninja") ExtractZip(item.File, PathCombine(staging, L"ninja"));
        }
    } catch (...) {
        sdk.join();
        throw;
    }
    sdk.join();
    if (sdkFailure) std::rethrow_exception(sdkFailure);
    // msiexec leaves a copy of each package; a scanner may still be reading one, and a leftover costs only space.
    wstring sdkDir = PathCombine(staging, L"sdk");
    if (DirExists(sdkDir))
        for (const auto& file : ListFiles(sdkDir, false))
            if (EndsWith(file, L".msi", true)) DeleteFileQuietly(file);

    MoveDirectory(staging, root_, log);
    if (!Locate(true)) Fail(ErrorKind::InvalidOperation, L"The unpacked toolchain is incomplete.");
    Json marker = Json::Object();
    marker.Set(L"id", Json::Str(id_));
    marker.Set(L"sdk", Json::Str(sdkVersion_));
    marker.Set(L"createdUtc", Json::Str(UtcNowIso()));
    SaveJson(Marker(), marker);
    try {
        DeleteTree(downloads);
    } catch (const Error& error) {
        log(L"Could not remove the toolchain downloads: " + error.message);
    }
    // Toolchains of earlier versions are never used again.
    for (const auto& dir : ListDirs(DirName(root_), false))
        if (!IEquals(dir, root_)) try { DeleteTree(dir); } catch (const Error&) {}
    log(L"Toolchain " + id_ + L" is ready.");
}

bool Toolchain::Locate(bool strict) {
    wstring msvcRoot = PathCombine(root_, L"VC", L"Tools", L"MSVC");
    wstring sdkRoot = PathCombine(root_, L"sdk", L"Windows Kits", L"10");
    if (!DirExists(msvcRoot) || !DirExists(PathCombine(sdkRoot, L"Include"))) return false;
    msvcDir_ = LastSorted(ListDirs(msvcRoot, false));
    wstring include = LastSorted(ListDirs(PathCombine(sdkRoot, L"Include"), false));
    if (msvcDir_.empty() || include.empty()) return false;
    sdkDir_ = sdkRoot;
    sdkVersion_ = FileName(include);
    wstring redist = PathCombine(root_, L"VC", L"Redist", L"MSVC");
    redistDir_.clear();
    if (DirExists(redist)) {
        for (const auto& d : ListDirs(redist, false)) {
            wstring crt = PathCombine(d, L"x64", L"Microsoft.VC143.CRT");
            if (FileExists(PathCombine(crt, L"vcruntime140.dll"))) { redistDir_ = crt; break; }
        }
    }
    std::vector<wstring> required = {ClangCl(), LldLink(), Rc(), Mt(), CMake(), Ninja(),
                                     PathCombine(msvcDir_, L"include", L"vcruntime.h"),
                                     PathCombine(msvcDir_, L"lib", L"x64", L"msvcrt.lib"),
                                     PathCombine(sdkDir_, L"Lib", sdkVersion_, L"um", L"x64", L"kernel32.lib"),
                                     PathCombine(sdkDir_, L"Lib", sdkVersion_, L"ucrt", L"x64", L"ucrt.lib")};
    for (const auto& path : required) {
        if (!FileExists(path)) {
            if (strict) Fail(ErrorKind::NotFound, L"The toolchain is missing " + path);
            return false;
        }
    }
    if (redistDir_.empty()) {
        if (strict) Fail(ErrorKind::NotFound, L"The toolchain has no C++ runtime redistributable files.");
        return false;
    }
    return true;
}

// A complete, isolated build environment: nothing from an installed Visual Studio or the user's PATH.
Environment Toolchain::BuildEnvironment() {
    Locate(true);
    Environment env;
    for (const wchar_t* name : {L"SystemRoot", L"windir", L"SystemDrive", L"TEMP", L"TMP", L"USERPROFILE", L"LOCALAPPDATA", L"APPDATA",
                                L"COMSPEC", L"PATHEXT", L"OS", L"NUMBER_OF_PROCESSORS", L"PROCESSOR_ARCHITECTURE",
                                L"PROCESSOR_IDENTIFIER", L"USERNAME", L"HOMEDRIVE", L"HOMEPATH"}) {
        auto value = GetEnv(name);
        if (value) env.emplace_back(name, *value);
    }
    wstring system = KnownFolder(FOLDERID_System);
    wstring windows = KnownFolder(FOLDERID_Windows);
    env.emplace_back(L"PATH", Join({DirName(ClangCl()), PathCombine(sdkDir_, L"bin", sdkVersion_, L"x64"), DirName(CMake()), DirName(Ninja()),
                                    system, windows, PathCombine(system, L"Wbem")}, L";"));
    wstring inc = PathCombine(sdkDir_, L"Include", sdkVersion_);
    env.emplace_back(L"INCLUDE", Join({PathCombine(msvcDir_, L"include"), PathCombine(inc, L"ucrt"), PathCombine(inc, L"shared"),
                                       PathCombine(inc, L"um")}, L";"));
    wstring lib = PathCombine(sdkDir_, L"Lib", sdkVersion_);
    env.emplace_back(L"LIB", Join({PathCombine(msvcDir_, L"lib", L"x64"), PathCombine(lib, L"ucrt", L"x64"), PathCombine(lib, L"um", L"x64")}, L";"));
    // Without this, clang-cl and lld-link search the machine for a Visual C++ and use its version.
    env.emplace_back(L"VCToolsInstallDir", msvcDir_ + L"\\");
    return env;
}

std::vector<wstring> Toolchain::CMakeToolArguments() {
    Locate(true);
    auto forward = [](const wstring& p) { return Replace(p, L"\\", L"/"); };
    return {L"-DCMAKE_CXX_COMPILER=" + forward(ClangCl()),
            L"-DCMAKE_CXX_FLAGS_INIT=-fms-compatibility-version=" + MsvcCompilerVersion(),
            L"-DCMAKE_LINKER=" + forward(LldLink()),
            L"-DCMAKE_RC_COMPILER=" + forward(Rc()),
            L"-DCMAKE_MT=" + forward(Mt()),
            L"-DCMAKE_MAKE_PROGRAM=" + forward(Ninja())};
}

std::vector<wstring> Toolchain::RuntimeDlls() {
    Locate(true);
    std::vector<wstring> dlls;
    for (const auto& file : ListFiles(redistDir_, false))
        if (EndsWith(file, L".dll", true)) dlls.push_back(file);
    return dlls;
}

// ---- the exact source --------------------------------------------------------------------------------------------

namespace {
class SourceTree {
public:
    explicit SourceTree(Json manifest) : manifest_(std::move(manifest)) {
        files_ = JObj(&manifest_, L"files");
        if (Commit().empty() || !files_ || files_->members.empty()) Fail(ErrorKind::InvalidData, L"The setup's source manifest is incomplete.");
        for (const auto& member : files_->members) lookup_[member.first] = JScalarText(member.second);
    }

    wstring Commit() const { return JStr(&manifest_, L"commit").value_or(L""); }
    wstring Repository() const { return JStr(&manifest_, L"repository").value_or(L""); }
    wstring Version() const { return JStr(&manifest_, L"version").value_or(L""); }
    wstring ShortCommit() const { return Commit().substr(0, std::min<size_t>(12, Commit().size())); }

    // SHA-256 over a subset of the manifest, used for compatibility keys.
    wstring SubsetSha256(const std::vector<wstring>& prefixes) const {
        std::vector<wstring> keys;
        for (const auto& member : files_->members) keys.push_back(member.first);
        std::sort(keys.begin(), keys.end());
        wstring text;
        for (const auto& key : keys) {
            for (const auto& prefix : prefixes) {
                if (StartsWith(key, prefix)) {
                    text += key;
                    text += L'\0';
                    text += lookup_.at(key);
                    text += L'\n';
                    break;
                }
            }
        }
        return Sha256Text(text);
    }

    // The verified source under sourcesDirectory: a copy verified earlier, else --source (used or rejected), else the
    // embedded copy, else GitHub's archive of the commit.
    wstring Ensure(const wstring& sourcesDirectory, const std::optional<wstring>& overridePath,
                   const std::function<void(const wstring&, double)>& progress, const std::function<void(const wstring&)>& log,
                   const CancelToken& cancel) {
        wstring target = PathCombine(sourcesDirectory, ShortCommit());
        if (DirExists(target)) {
            try {
                Verify(target);
                log(L"Using the verified source at " + target + L".");
                return target;
            } catch (const Error& error) {
                if (error.kind != ErrorKind::InvalidData) throw;
                DeleteTree(target);
            }
        }
        CreateDirs(sourcesDirectory);
        wstring staging = target + L".partial";
        if (overridePath && !overridePath->empty()) {
            Unpack(*overridePath, staging, progress);
            Verify(staging);
            log(L"Using the source at " + *overridePath + L".");
        } else if (!TryEmbedded(staging, progress, log)) {
            Download(sourcesDirectory, staging, progress, log, cancel);
        }
        MoveDirectory(staging, target, log);
        log(L"Verified " + Num((long long)files_->members.size()) + L" source files of revision " + Commit() + L".");
        return target;
    }

private:
    bool TryEmbedded(const wstring& staging, const std::function<void(const wstring&, double)>& progress,
                     const std::function<void(const wstring&)>& log) {
        if (!payload::Has(L"nebula-source.zip")) return false;
        try {
            progress(L"Unpacking the Nebula source", 0);
            Bytes zip = payload::ReadBytes(L"nebula-source.zip");
            auto archive = ZipReader::OpenMemory(zip.data(), zip.size());
            Extract(*archive, staging);
            Verify(staging);
            log(L"Using the source embedded in Setup.");
            return true;
        } catch (const Error& error) {
            if (error.kind == ErrorKind::InvalidData) log(L"The source embedded in Setup does not match this release (" + error.message + L").");
            else if (error.kind == ErrorKind::Io || error.kind == ErrorKind::Access || error.kind == ErrorKind::NotFound)
                log(L"Could not unpack the source embedded in Setup: " + error.message);
            else throw;
        }
        return false;
    }

    void Download(const wstring& sourcesDirectory, const wstring& staging, const std::function<void(const wstring&, double)>& progress,
                  const std::function<void(const wstring&)>& log, const CancelToken& cancel) {
        wstring zip = PathCombine(sourcesDirectory, L"nebula-" + Commit() + L".zip");
        wstring url = L"https://codeload.github.com/" + Repository() + L"/zip/" + Commit();
        log(L"Downloading the Nebula source from " + url);
        try {
            http::Fetch(url, zip, L"", 0, [&](long long bytes) { progress(L"Downloading the Nebula source (" + FormatBytes(bytes) + L")", 0); }, cancel);
            Unpack(zip, staging, progress);
            Verify(staging);
        } catch (const Error& error) {
            if (error.kind == ErrorKind::Web) throw Unavailable(error.message);
            if (error.kind == ErrorKind::InvalidData) {
                DeleteFileQuietly(zip);
                throw Unavailable(error.message);
            }
            throw;
        }
        DeleteFileQuietly(zip);
    }

    Error Unavailable(const wstring& reason) const {
        return Error(ErrorKind::InvalidOperation, L"Setup could not get the Nebula " + Version() + L" source (revision " + ShortCommit() +
                                                      L") from GitHub: " + reason + L"\r\n\r\nCheck the internet connection and run Setup again.");
    }

    void Unpack(const wstring& path, const wstring& staging, const std::function<void(const wstring&, double)>& progress) {
        if (DirExists(path)) {
            progress(L"Copying the Nebula source", 0);
            if (DirExists(staging)) DeleteTree(staging);
            for (const auto& member : files_->members) {
                wstring source = PathCombine(path, Slashes(member.first));
                if (!FileExists(source)) Fail(ErrorKind::InvalidData, L"The source is missing " + member.first + L".");
                wstring destination = SafeJoin(staging, member.first);
                CreateDirs(DirName(destination));
                CopyFileOrThrow(source, destination, false);
            }
            return;
        }
        progress(L"Unpacking the Nebula source", 0);
        auto archive = ZipReader::OpenFile(path);
        Extract(*archive, staging);
    }

    // The archive's files below its top folder (git archive --prefix, GitHub's archive).
    void Extract(const ZipReader& archive, const wstring& staging) {
        if (DirExists(staging)) DeleteTree(staging);
        for (const auto& entry : archive.Entries()) {
            wstring name = Replace(Widen(entry.name), L"\\", L"/");
            size_t slash = name.find(L'/');
            if (slash == wstring::npos || EndsWith(name, L"/")) continue;
            wstring relative = name.substr(slash + 1);
            if (!lookup_.count(relative)) Fail(ErrorKind::InvalidData, L"The source archive contains an unexpected file: " + relative);
            archive.ExtractTo(entry, SafeJoin(staging, relative), false);
        }
    }

    void Verify(const wstring& root) {
        for (const auto& member : files_->members) {
            wstring path = PathCombine(root, Slashes(member.first));
            if (!FileExists(path)) Fail(ErrorKind::InvalidData, L"The source is missing " + member.first + L".");
            if (Sha256File(path) != lookup_[member.first]) Fail(ErrorKind::InvalidData, L"The source file " + member.first + L" does not match this release.");
        }
        if (ListFiles(root, true).size() != files_->members.size())
            Fail(ErrorKind::InvalidData, L"The source folder contains files that are not part of this release.");
    }

    Json manifest_;
    const Json* files_ = nullptr;
    std::map<wstring, wstring> lookup_;
};

// ---- relinking retained objects ----------------------------------------------------------------------------------

// Compiled game objects stay on the user's machine; they allow runtime-library updates without recompiling Wii code.
namespace relink {
const wchar_t* const kTargets[] = {L"game", L"home_button", L"dsp"};
const wchar_t* const kLibraries[] = {L"galaxy_ppc_float.lib", L"galaxy_softfloat.lib", L"galaxy_dsp_alu.lib"};
const wchar_t* const kSystemLibraries[] = {L"kernel32.lib", L"user32.lib", L"gdi32.lib", L"winspool.lib", L"shell32.lib",
                                           L"ole32.lib", L"oleaut32.lib", L"uuid.lib", L"comdlg32.lib", L"advapi32.lib"};

wstring CheckedPath(const wstring& directory, const wstring& relative) {
    if (relative.empty() || IsRooted(relative) || relative.find(L':') != wstring::npos || !EndsWith(relative, L".obj", true))
        Fail(ErrorKind::InvalidData, L"Invalid retained object path.");
    wstring root = TrimEnd(FullPath(directory), L"\\") + L"\\";
    wstring full = FullPath(PathCombine(root, Slashes(relative)));
    if (!StartsWith(full, root, true)) Fail(ErrorKind::InvalidData, L"Invalid retained object path.");
    return full;
}

void RecordOutputs(Json& manifest, const wstring& app, const wstring& libraries) {
    Json modules = Json::Object();
    for (const wchar_t* t : kTargets) {
        wstring name = wstring(L"RMGE01_") + t + L".dll";
        modules.Set(name, Json::Str(Sha256File(PathCombine(app, name))));
    }
    Json libs = Json::Object();
    for (const wchar_t* n : kLibraries) libs.Set(n, Json::Str(Sha256File(PathCombine(libraries, n))));
    manifest.Set(L"modules", std::move(modules));
    manifest.Set(L"libraries", std::move(libs));
}

void Capture(const wstring& build, const wstring& app, const wstring& libraries, const wstring& toolchain) {
    wstring cache = PathCombine(app, L"relink-cache");
    CreateDirs(cache);
    Json objects = Json::Object();
    auto lines = ReadAllLines(PathCombine(build, L"build.ninja"));
    for (const wchar_t* target : kTargets) {
        wstring subdir = wstring(target) == L"home_button" ? L"home" : target;
        wstring marker = subdir + L"\\RMGE01_" + target + L".dll ";
        std::vector<wstring> matches;
        for (const auto& line : lines)
            if (StartsWith(line, L"build ") && StartsWith(Replace(line, L"/", L"\\").substr(6), marker)) matches.push_back(line);
        if (matches.size() != 1) Fail(ErrorKind::InvalidOperation, L"build.ninja does not name exactly one " + marker);
        const wstring& line = matches[0];
        wstring inputs = line.substr(line.find(L": ") + 2);
        inputs = inputs.substr(inputs.find(L' ') + 1);
        inputs = Split(inputs, L'|')[0];
        Json entries = Json::Array();
        for (const auto& relative : Split(inputs, L' ')) {
            if (relative.empty()) continue;
            wstring from = CheckedPath(build, relative), to = CheckedPath(cache, relative);
            if (IsReparsePoint(from)) Fail(ErrorKind::Io, L"Linked object file rejected.");
            CreateDirs(DirName(to));
            CopyFileOrThrow(from, to, false);
            Json entry = Json::Object();
            entry.Set(L"path", Json::Str(relative));
            entry.Set(L"sha256", Json::Str(Sha256File(to)));
            entries.Add(std::move(entry));
        }
        if (entries.items.empty()) Fail(ErrorKind::InvalidData, wstring(L"No compiled objects retained for ") + target);
        objects.Set(target, std::move(entries));
    }
    Json manifest = Json::Object();
    manifest.Set(L"schema", Json::Str(L"nebula.relink.v1"));
    manifest.Set(L"toolchain", Json::Str(toolchain));
    manifest.Set(L"objects", std::move(objects));
    RecordOutputs(manifest, app, libraries);
    SaveJson(PathCombine(cache, L"manifest.json"), manifest);
}

bool Update(const wstring& current, const wstring& app, const wstring& libraries, const wstring& toolchain,
            const std::function<int(const std::vector<wstring>&)>& link, const std::function<void(const wstring&)>& log) {
    wstring oldCache = PathCombine(current, L"relink-cache");
    wstring manifestPath = PathCombine(oldCache, L"manifest.json");
    if (!FileExists(manifestPath)) return false;
    Json manifest = LoadJson(manifestPath);
    if (JStr(&manifest, L"schema").value_or(L"") != L"nebula.relink.v1" || JStr(&manifest, L"toolchain").value_or(L"") != toolchain)
        Fail(ErrorKind::InvalidData, L"The compiled game objects need the toolchain they were built with.");
    for (const wchar_t* target : kTargets) {
        wstring name = wstring(L"RMGE01_") + target + L".dll";
        if (Sha256File(PathCombine(current, name)) != JStr(JObj(&manifest, L"modules"), name).value_or(L""))
            Fail(ErrorKind::InvalidData, L"Retained objects do not belong to the installed " + name);
        const Json* list = JList(JObj(&manifest, L"objects"), target);
        if (!list) Fail(ErrorKind::InvalidData, L"A compiled game object was modified.");
        for (const auto& entry : list->items)
            if (Sha256File(CheckedPath(oldCache, JStr(&entry, L"path").value_or(L""))) != JStr(&entry, L"sha256").value_or(L""))
                Fail(ErrorKind::InvalidData, L"A compiled game object was modified.");
    }
    wstring cache = PathCombine(app, L"relink-cache");
    CopyTreeWithStreams(oldCache, cache, nullptr);
    bool changed = false;
    for (const wchar_t* n : kLibraries)
        if (Sha256File(PathCombine(libraries, n)) != JStr(JObj(&manifest, L"libraries"), n).value_or(L"")) changed = true;
    if (changed) {
        for (const wchar_t* target : kTargets) {
            std::vector<wstring> response;
            for (const auto& entry : JList(JObj(&manifest, L"objects"), target)->items)
                response.push_back(CheckedPath(cache, JStr(&entry, L"path").value_or(L"")));
            if (wstring(target) == L"dsp") response.push_back(PathCombine(libraries, kLibraries[2]));
            else {
                response.push_back(PathCombine(libraries, kLibraries[0]));
                response.push_back(PathCombine(libraries, kLibraries[1]));
            }
            for (const wchar_t* lib : kSystemLibraries) response.push_back(lib);
            wstring rsp = PathCombine(app, wstring(target) + L"-relink.rsp");
            wstring text;
            for (const auto& line : response) text += QuoteArg(line) + L"\r\n";
            WriteAllText(rsp, text);
            std::vector<wstring> args = {L"/nologo", L"@" + rsp, L"/dll", L"/machine:x64", L"/INCREMENTAL:NO", L"/version:0.0",
                                         L"/out:" + PathCombine(app, wstring(L"RMGE01_") + target + L".dll"),
                                         L"/implib:" + PathCombine(app, wstring(target) + L"-relink.lib")};
            if (wstring(target) == L"dsp")
                for (const wchar_t* exportName : {L"entry", L"entry_expected_iram_start_address", L"entry_expected_iram_byte_len",
                                                  L"entry_expected_iram_sha1", L"entry_generated_probe_contract_version",
                                                  L"entry_generated_probe_capabilities"})
                    args.push_back(wstring(L"/EXPORT:galaxy_rmge01_dsp_") + exportName);
            if (link(args) != 0) Fail(ErrorKind::InvalidOperation, wstring(L"Relinking ") + target + L" failed.");
            DeleteFileOrThrow(rsp);
            DeleteFileOrThrow(PathCombine(app, wstring(target) + L"-relink.lib"));
            DeleteFileOrThrow(PathCombine(app, wstring(target) + L"-relink.exp"));
        }
        log(L"Relinked the game modules against the new runtime libraries.");
    } else {
        log(L"Runtime libraries unchanged; the game modules were kept.");
    }
    RecordOutputs(manifest, app, libraries);
    SaveJson(PathCombine(cache, L"manifest.json"), manifest);
    return true;
}
}  // namespace relink

// ---- module compatibility (the DLLs' exported ABI contracts) -----------------------------------------------------

class Library {
public:
    explicit Library(const wstring& path) {
        handle_ = LoadLibraryExW(FullPath(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!handle_) Fail(ErrorKind::InvalidData, L"Could not load " + FileName(path) + L" (error " + Num(GetLastError()) + L").");
    }
    ~Library() { FreeLibrary(handle_); }
    FARPROC Address(const char* name) {
        FARPROC address = GetProcAddress(handle_, name);
        if (!address) Fail(ErrorKind::InvalidData, L"The installed game module is missing " + Widen(name) + L". No game code was rebuilt.");
        return address;
    }

private:
    HMODULE handle_;
};

void CheckModuleVersions(const wstring& directory, const Json& contract) {
    uint32_t game, home, rso, dsp, capabilities;
    using Manifest = const uint8_t* (*)();
    using Number = uint32_t (*)();
    {
        Library module(PathCombine(directory, L"RMGE01_game.dll"));
        for (const char* name : {"galaxy_module_init", "galaxy_module_entry", "galaxy_lookup_function"}) module.Address(name);
        auto ptr = reinterpret_cast<Manifest>(module.Address("galaxy_module_manifest"))();
        if (!ptr || *reinterpret_cast<const int32_t*>(ptr) < 64 || std::string(reinterpret_cast<const char*>(ptr + 8), 6) != "RMGE01")
            Fail(ErrorKind::InvalidData, L"The installed game manifest is invalid.");
        game = *reinterpret_cast<const uint32_t*>(ptr + 4);
    }
    {
        Library module(PathCombine(directory, L"RMGE01_home_button.dll"));
        for (const char* name : {"galaxy_home_button_rso_try_call", "galaxy_home_button_rso_resume"}) module.Address(name);
        auto ptr = reinterpret_cast<Manifest>(module.Address("galaxy_home_button_rso_manifest"))();
        if (!ptr || *reinterpret_cast<const int32_t*>(ptr) < 100 || std::string(reinterpret_cast<const char*>(ptr + 12), 6) != "RMGE01")
            Fail(ErrorKind::InvalidData, L"The installed Home manifest is invalid.");
        rso = *reinterpret_cast<const uint32_t*>(ptr + 4);
        home = *reinterpret_cast<const uint32_t*>(ptr + 8);
    }
    {
        Library module(PathCombine(directory, L"RMGE01_dsp.dll"));
        for (const char* name : {"galaxy_rmge01_dsp_entry", "galaxy_rmge01_dsp_entry_expected_iram_sha1",
                                 "galaxy_rmge01_dsp_entry_expected_iram_start_address", "galaxy_rmge01_dsp_entry_expected_iram_byte_len"})
            module.Address(name);
        dsp = reinterpret_cast<Number>(module.Address("galaxy_rmge01_dsp_entry_generated_probe_contract_version"))();
        capabilities = reinterpret_cast<Number>(module.Address("galaxy_rmge01_dsp_entry_generated_probe_capabilities"))();
    }
    uint32_t requiredNative = uint32_t(JLong(&contract, L"nativeAbi")), requiredRso = uint32_t(JLong(&contract, L"rsoAbi")),
             requiredDsp = uint32_t(JLong(&contract, L"dspProbeVersion")), requiredCapabilities = uint32_t(JLong(&contract, L"dspCapabilities"));
    if (game != requiredNative || home != requiredNative || rso != requiredRso || dsp != requiredDsp ||
        (capabilities & requiredCapabilities) != requiredCapabilities)
        Fail(ErrorKind::InvalidOperation, L"This release is not compatible with the installed game modules. "
                                          L"The installed modules cannot be reused with this runtime.");
}

// ---- system checks -----------------------------------------------------------------------------------------------

long long FreeBytes(const wstring& path) {
    wstring probe = FullPath(path);
    while (!DirExists(probe)) {
        wstring parent = DirName(probe);
        if (parent.empty() || parent == probe) break;
        probe = parent;
    }
    ULARGE_INTEGER available, total, free;
    if (!GetDiskFreeSpaceExW(probe.c_str(), &available, &total, &free)) FailWin32(GetLastError(), probe);
    return (long long)available.QuadPart;
}

wstring FileSystemName(const wstring& path) {
    wstring full = FullPath(path);
    wstring root;
    if (StartsWith(full, L"\\\\")) {
        auto parts = Split(full.substr(2), L'\\');
        root = parts.size() >= 2 ? L"\\\\" + parts[0] + L"\\" + parts[1] + L"\\" : full;
    } else {
        root = full.substr(0, 3);
    }
    wchar_t name[64] = L"";
    if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, name, 64)) return L"unknown";
    return name;
}

unsigned long long TotalPhysicalMemory() {
    MEMORYSTATUSEX status{sizeof(status)};
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
}

wstring ElapsedMinutes(ULONGLONG ms) {
    wchar_t buffer[32];
    ULONGLONG seconds = ms / 1000;
    swprintf_s(buffer, L"%02llu:%02llu", (seconds / 60) % 60, seconds % 60);
    return buffer;
}
}  // namespace

// ---- the engine --------------------------------------------------------------------------------------------------

static std::optional<bool> g_avx2;
static std::optional<wstring> g_runtimeIsa;
static bool g_dualRuntime = false;

bool InstallEngine::HostSupportsAvx2() {
    if (!g_avx2) g_avx2 = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != FALSE;
    return *g_avx2;
}

// The ISA of NebulaRuntime.exe and of the float and DSP libraries this install uses: a "SSE2+AVX2" package carries
// both builds (AVX2 under "avx2/") and installs the AVX2 one when this CPU exposes AVX2.
wstring InstallEngine::RuntimeNativeIsa() {
    if (!g_runtimeIsa) {
        wstring value = L"SSE2";
        try { value = ToUpper(Trim(Widen(payload::ReadText(L"runtime-isa.txt")))); } catch (const Error&) {}
        g_dualRuntime = value == L"SSE2+AVX2";
        g_runtimeIsa = g_dualRuntime ? (HostSupportsAvx2() ? L"AVX2" : L"SSE2") : value == L"AVX2" ? L"AVX2" : L"SSE2";
    }
    return *g_runtimeIsa;
}

static wstring RuntimePayload(const wstring& name) {
    return InstallEngine::RuntimeNativeIsa() == L"AVX2" && g_dualRuntime ? L"avx2/" + name : name;
}

// The game modules are compiled on the machine that runs them; NEBULA_MODULE_ISA=SSE2 forces the portable baseline.
static wstring ModuleNativeIsa() {
    auto forced = GetEnv(L"NEBULA_MODULE_ISA");
    if (forced && IEquals(*forced, L"SSE2")) return L"SSE2";
    return InstallEngine::HostSupportsAvx2() ? L"AVX2" : L"SSE2";
}

static const std::vector<wstring> kModuleFiles = {L"RMGE01_game.dll", L"RMGE01_home_button.dll", L"RMGE01_dsp.dll", L"RMGE01_boot_image.bin"};
static const std::vector<wstring> kRuntimeLibraries = {L"galaxy_ppc_float.lib", L"galaxy_softfloat.lib", L"galaxy_dsp_alu.lib"};
static const std::vector<wstring> kGameInputFiles = {L"sys/main.dol", L"sys/boot.bin", L"sys/bi2.bin", L"sys/apploader.img", L"sys/fst.bin",
                                                     L"files/ModuleData/HomeButtonMenuWrapperRSO.rso", L"files/ModuleData/product.sel"};

static bool RetainedInputsValid(const InstallLayout& layout) {
    wstring manifest = PathCombine(layout.GameInputs(), L"inputs.json");
    if (!FileExists(manifest)) return false;
    try {
        Json hashes = LoadJson(manifest);
        for (const auto& relative : kGameInputFiles) {
            wstring path = PathCombine(layout.GameInputs(), Slashes(relative));
            if (!FileExists(path) || Sha256File(path) != JStr(&hashes, relative).value_or(L"")) return false;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

static bool ContentValid(const InstallLayout& layout) {
    wstring marker = PathCombine(layout.Content(), L"content.json");
    if (!FileExists(marker) || !FileExists(PathCombine(layout.Content(), L"game.pak"))) return false;
    try {
        Json loaded = LoadJson(marker);
        return JLong(&loaded, L"formatVersion") == InstallEngine::kContentFormatVersion;
    } catch (const std::exception&) {
        return false;
    }
}

bool InstallEngine::RetainedGameValid(const InstallLayout& layout) { return RetainedInputsValid(layout) && ContentValid(layout); }

std::vector<wstring> InstallEngine::CheckRequirements(const wstring& installRoot) {
    std::vector<wstring> problems;
    if (!IsWindows10OrGreater()) problems.push_back(L"Nebula needs Windows 10 or 11.");
    if (RuntimeNativeIsa() == L"AVX2" && !HostSupportsAvx2())
        problems.push_back(L"This package's runtime was built for AVX2, which this CPU does not expose. Use a package built with -NativeIsa SSE2, or install on a CPU with AVX2.");
    try {
        wstring fs = FileSystemName(installRoot);
        if (!IEquals(fs, L"NTFS")) problems.push_back(L"The install folder must be on an NTFS drive (this one is " + fs + L").");
        long long free = FreeBytes(installRoot);
        if (free < kRequiredFreeBytes)
            problems.push_back(L"Setup needs " + FormatBytes(kRequiredFreeBytes) + L" free; the drive has " + FormatBytes(free) + L".");
    } catch (const std::exception& error) {
        problems.push_back(L"Cannot inspect the install folder: " + ErrorText(error));
    }
    return problems;
}

// The Microsoft C++ Build Tools license is asked for only when this install downloads the compiler.
bool InstallEngine::BuildToolsLicenseRequired(InstallMode mode, Toolchain& toolchain) {
    return mode != InstallMode::Update && !toolchain.IsReady();
}

std::optional<wstring> InstallEngine::LowMemoryWarning() {
    unsigned long long memory = TotalPhysicalMemory();
    if (memory >= kMinimumMemory) return std::nullopt;
    return L"This PC has " + FormatBytes((long long)memory) + L" of RAM. Nebula is designed for 8 GB or more.\r\n\r\n"
           L"Installing with less is experimental: compiling the game takes much longer, and the game may stutter, run slowly or "
           L"close unexpectedly.\r\n\r\n"
           L"If you continue, you do so at your own risk. Nebula's developer is not responsible for anything that happens while "
           L"the game runs on this PC.";
}

// One compiler per thread, limited to about 1 GB of RAM each beyond 3 GB for Windows.
static int DefaultCompileJobs() {
    double gigabytes = double(TotalPhysicalMemory()) / double(1ULL << 30);
    int byMemory = int(std::floor(gigabytes - 3));
    int processors = int(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    return std::max(1, std::min(std::min(processors, byMemory), 64));
}

struct InstallEngine::Impl {
    InstallRequest request;
    Progress& progress;
    CancelToken& cancel;
    InstallLayout layout;
    SourceTree source;
    Toolchain toolchain;
    HANDLE logFile = INVALID_HANDLE_VALUE;
    std::mutex logLock;
    ULONGLONG started = GetTickCount64();
    wstring staging;
    bool preservingModules = false;
    double stageBase = 0, stageWeight = 0;
    wstring stepText;
    std::atomic<bool> waitingForDownloads{false}, waitingForDlss{false};
    InstallEngine& owner;

    Impl(InstallRequest r, Progress& p, CancelToken& c, InstallEngine& o)
        : request(std::move(r)), progress(p), cancel(c), layout(request.InstallRoot), source(PayloadJson(L"source-manifest.json")),
          toolchain(PayloadJson(L"toolchain.json"), layout.Toolchains()), owner(o) {
        CreateDirs(paths::SetupLogs());
        wstring logPath = PathCombine(paths::SetupLogs(), UtcStamp(true) + L"-" + NewGuidN() + L".log");
        logFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logFile == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), logPath);
        owner.LogPath = logPath;
    }

    ~Impl() {
        if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
    }

    // Everything compiled modules depend on: generator, runtime, toolchain and recipe.
    wstring ModuleKey() {
        wstring inputs = source.SubsetSha256({L"crates/", L"metadata/", L"third_party/", L"runtime/", L"Cargo.lock", L"Cargo.toml", L"CMakeLists.txt"});
        std::vector<wstring> parts = {inputs, toolchain.Id(), L"Ninja", L"Release", L"clang-cl", L"GALAXY_NATIVE_ISA=" + ModuleNativeIsa(),
                                      L"GALAXY_MODULE_USE_PCH=ON", L"runtime-isa=" + RuntimeNativeIsa()};
        return Sha256Text(Join(parts, L"\n"));
    }

    void WriteLog(const wstring& line) {
        std::lock_guard<std::mutex> guard(logLock);
        if (logFile == INVALID_HANDLE_VALUE) return;
        std::string text = Narrow(UtcClock() + L" " + line + L"\r\n");
        DWORD written = 0;
        WriteFile(logFile, text.data(), DWORD(text.size()), &written, nullptr);
    }

    void Log(const wstring& line) {
        WriteLog(line);
        progress.Log(line);
    }

    void Report(double overall, const wstring& text, double fraction) { progress.Report(overall, text, fraction); }

    void Stage(double start, double weight, const wstring& text) {
        cancel.ThrowIfCanceled();
        stageBase = start;
        stageWeight = weight;
        stepText = text;
        Log(L"== " + text);
        Report(start, text, -1);
    }

    void StepProgress(const std::optional<wstring>& text, double fraction) {
        if (text) stepText = *text;
        Report(stageBase + stageWeight * std::max(0.0, std::min(1.0, fraction)), stepText, fraction);
    }

    // Background downloads show progress only once Setup is waiting for them.
    void BackgroundProgress(const wstring& text, double fraction) {
        if (waitingForDownloads) StepProgress(text, fraction);
    }

    wstring RecompPath() {
        wstring path = PathCombine(layout.Staging(), L"nebula-recomp-" + wstring(build::kVersion) + L".exe");
        if (!FileExists(path)) payload::Extract(L"nebula-recomp.exe", path);
        return path;
    }

    int RunTool(const wstring& file, const std::vector<wstring>& arguments, const Environment* env, const std::function<void(const wstring&)>& onLine) {
        Log(L"> " + FileName(file) + L" " + JoinArgs(arguments));
        ProcessRunner runner;
        return runner.Run(file, arguments, staging, env, onLine, cancel);
    }

    void RunRecomp(const std::vector<wstring>& arguments, const std::function<bool(const wstring&)>& filter) {
        int code = RunTool(RecompPath(), arguments, nullptr, [&](const wstring& line) {
            if (filter && filter(line)) return;
            if (StartsWith(line, L"STEP ")) {
                size_t space = line.find(L' ', 5);
                if (space != wstring::npos) StepProgress(line.substr(space + 1), -1);
            }
            Log(line);
        });
        if (code != 0) Fail(ErrorKind::InvalidOperation, L"nebula-recomp " + arguments[0] + L" failed (exit " + Num(code) + L"). See the setup log.");
    }

    void CMake(const std::vector<wstring>& arguments, const Environment& env) {
        std::optional<wstring> last;
        int code = RunTool(toolchain.CMake(), arguments, &env, [&](const wstring& line) {
            // Ninja's [done/total] lines are progress.
            if (line.size() > 3 && line[0] == L'[') {
                size_t slash = line.find(L'/'), close = line.find(L']');
                long long done = 0, total = 0;
                if (slash != wstring::npos && close != wstring::npos && slash < close && ParseInt(line.substr(1, slash - 1), done) &&
                    ParseInt(line.substr(slash + 1, close - slash - 1), total)) {
                    StepProgress(std::nullopt, double(done) / double(std::max(1LL, total)));
                    WriteLog(line);
                    return;
                }
            }
            if (line.find(L"error") != wstring::npos || line.find(L"FAILED") != wstring::npos) last = line;
            Log(line);
        });
        if (code != 0) Fail(ErrorKind::InvalidOperation, L"Compiling failed" + (last ? L": " + *last : wstring(L".")) + L" See the setup log.");
    }

    void Identify(const wstring& input) {
        std::vector<wstring> lines;
        int code = RunTool(RecompPath(), {L"identify", input, L"--json"}, nullptr, [&](const wstring& line) { lines.push_back(line); });
        wstring json = Join(lines, L"\n");
        Log(json);
        Json identity;
        try {
            size_t brace = json.find(L'{');
            identity = ParseJson(json.substr(brace == wstring::npos ? 0 : brace));
        } catch (const std::exception&) {
            Fail(ErrorKind::InvalidOperation, lines.empty() ? L"The selected file could not be read." : lines.back());
        }
        if (code != 0 || !JBool(&identity, L"supported")) Fail(ErrorKind::InvalidOperation, JStr(&identity, L"message").value_or(L""));
    }

    wstring PrepareData(const wstring& input) {
        if (DirExists(input)) {
            wstring nested = PathCombine(input, L"DATA");
            return FileExists(PathCombine(nested, L"sys", L"main.dol")) ? nested : input;
        }
        Stage(0.02, 0.08, L"Extracting your game");
        wstring output = PathCombine(staging, L"extracted");
        RunRecomp({L"extract", input, L"--output", output}, [&](const wstring& line) {
            // PROGRESS done total
            if (!StartsWith(line, L"PROGRESS ")) return false;
            auto parts = Split(line.substr(9), L' ');
            double done = 0, total = 0;
            if (parts.size() != 2 || !ParseDouble(parts[0], done) || !ParseDouble(parts[1], total)) return false;
            StepProgress(std::nullopt, done / std::max(1.0, total));
            return true;
        });
        return PathCombine(output, L"DATA");
    }

    void CopyGameInputs(const wstring& data, const wstring& target) {
        Json hashes = Json::Object();
        for (const auto& relative : kGameInputFiles) {
            wstring to = PathCombine(target, Slashes(relative));
            CreateDirs(DirName(to));
            CopyFileOrThrow(PathCombine(data, Slashes(relative)), to, false);
            hashes.Set(relative, Json::Str(Sha256File(to)));
        }
        SaveJson(PathCombine(target, L"inputs.json"), hashes);
    }

    // One clang-cl build of the game, Home Menu and DSP modules.
    void Build(const wstring& src, const wstring& gen, const wstring& app) {
        Environment env = toolchain.BuildEnvironment();
        int jobs = request.CompileJobs > 0 ? request.CompileJobs : DefaultCompileJobs();
        wstring libs = PathCombine(staging, L"runtime-libs");
        for (const auto& lib : kRuntimeLibraries) payload::Extract(RuntimePayload(lib), PathCombine(libs, lib));
        auto cmakePath = [](const wstring& path) { return Replace(path, L"\\", L"/"); };
        Stage(0.30, 0.64, L"Compiling the game (" + Num(jobs) + L" at a time)");
        wstring build = PathCombine(staging, L"build");
        std::vector<wstring> configure = {L"-S", gen, L"-B", build, L"-G", L"Ninja", L"-DCMAKE_BUILD_TYPE=Release",
                                          L"-DGALAXY_RUNTIME_INCLUDE=" + cmakePath(PathCombine(src, L"runtime", L"include")),
                                          L"-DGALAXY_PPC_FLOAT_LIBRARY=" + cmakePath(PathCombine(libs, L"galaxy_ppc_float.lib")),
                                          L"-DGALAXY_SOFTFLOAT_LIBRARY=" + cmakePath(PathCombine(libs, L"galaxy_softfloat.lib")),
                                          L"-DGALAXY_DSP_ALU_LIBRARY=" + cmakePath(PathCombine(libs, L"galaxy_dsp_alu.lib")),
                                          L"-DGALAXY_NATIVE_ISA=" + ModuleNativeIsa(), L"-DGALAXY_MODULE_COMPILE_JOBS=" + Num(jobs)};
        for (const auto& argument : toolchain.CMakeToolArguments()) configure.push_back(argument);
        CMake(configure, env);
        CMake({L"--build", build, L"--parallel", Num(jobs)}, env);
        CopyFileOrThrow(PathCombine(build, L"game", L"RMGE01_game.dll"), PathCombine(app, L"RMGE01_game.dll"), false);
        CopyFileOrThrow(PathCombine(build, L"home", L"RMGE01_home_button.dll"), PathCombine(app, L"RMGE01_home_button.dll"), false);
        CopyFileOrThrow(PathCombine(build, L"dsp", L"RMGE01_dsp.dll"), PathCombine(app, L"RMGE01_dsp.dll"), false);
        CopyFileOrThrow(PathCombine(gen, L"RMGE01_boot_image.bin"), PathCombine(app, L"RMGE01_boot_image.bin"), false);
        relink::Capture(build, app, libs, toolchain.Id());
    }

    void RebuildForUpdate(const wstring& app) {
        cancel.ThrowIfCanceled();
        if (!RetainedInputsValid(layout) || !ContentValid(layout))
            Fail(ErrorKind::InvalidOperation, L"The game modules must be rebuilt for this update. Run Setup again and select your game.");
        preservingModules = false;
        owner.ModuleUpdateNotice.reset();
        Stage(0.20, 0.70, L"Rebuilding the game modules");
        wstring expected = FullPath(PathCombine(staging, L"app"));
        if (!IEquals(FullPath(app), expected)) Fail(ErrorKind::InvalidOperation, L"Invalid update staging path.");
        DeleteTree(app);
        CreateDirs(app);
        toolchain.Ensure([this](const wstring& t, double f) { BackgroundProgress(t, f); }, [this](const wstring& l) { Log(l); }, cancel);
        wstring sourceDir = source.Ensure(layout.Sources(), request.SourceOverride, [this](const wstring& t, double f) { BackgroundProgress(t, f); },
                                          [this](const wstring& l) { Log(l); }, cancel);
        wstring gen = PathCombine(staging, L"fallback-generated");
        RunRecomp({L"generate", layout.GameInputs(), L"--output", gen}, nullptr);
        Build(sourceDir, gen, app);
    }

    // Seeds the DXBC and PSO caches so a first launch is warmed; a cache this PC already built is never replaced.
    void SeedShaderCache() {
        wstring cacheRoot = PathCombine(paths::ShaderCache(), L"RMGE01");
        for (const wchar_t* name : {L"shaders.bin", L"psos.bin"}) {
            wstring destination = PathCombine(cacheRoot, name);
            if (FileExists(destination)) {
                Log(wstring(L"Shader-cache seed: kept the existing ") + name + L".");
                continue;
            }
            if (!payload::Has(wstring(L"shadercache-seed.") + name)) {
                Log(wstring(L"Shader-cache seed: ") + name + L" is not in this package.");
                continue;
            }
            payload::Extract(wstring(L"shadercache-seed.") + name, destination);
            Log(wstring(L"Shader-cache seed: wrote ") + name + L".");
        }
    }

    void AssembleApp(const wstring& app, const std::optional<InstalledInfo>& current) {
        if (toolchain.IsReady())
            for (const auto& dll : toolchain.RuntimeDlls()) {
                wstring target = PathCombine(app, FileName(dll));
                if (!FileExists(target)) CopyFileOrThrow(dll, target, false);
            }
        if (!FileExists(PathCombine(app, L"vcruntime140.dll")) && current) current->CopyFiles(app, current->RuntimeDllNames());
        payload::Extract(RuntimePayload(L"NebulaRuntime.exe"), PathCombine(app, L"NebulaRuntime.exe"));
        for (const wchar_t* name : {L"Nebula.exe", L"NebulaSettings.exe", L"runtime-env.json", L"LICENSE", L"THIRD-PARTY-NOTICES.md",
                                    L"README.md", L"nebula-recomp.exe"})
            payload::Extract(name, PathCombine(app, name));
        WriteAllText(PathCombine(app, L"runtime-isa.txt"), RuntimeNativeIsa());

        Json files = Json::Object();
        for (const auto& path : ListFiles(app, false)) files.Set(FileName(path), Json::Str(Sha256File(path)));
        Json install = Json::Object();
        install.Set(L"schema", Json::Str(L"nebula.install.v1"));
        install.Set(L"version", Json::Str(build::kVersion));
        install.Set(L"commit", Json::Str(source.Commit()));
        install.Set(L"repository", Json::Str(source.Repository()));
        install.Set(L"moduleKey", Json::Str(preservingModules ? current->ModuleKey() : ModuleKey()));
        install.Set(L"moduleSourceCommit", Json::Str(preservingModules ? current->ModuleSourceCommit() : source.Commit()));
        install.Set(L"toolchain", Json::Str(toolchain.Id()));
        install.Set(L"contentFormatVersion", Json::Int(kContentFormatVersion));
        install.Set(L"runtimeIsa", Json::Str(RuntimeNativeIsa()));
        install.Set(L"moduleIsa", Json::Str(preservingModules ? current->ModuleIsa() : ModuleNativeIsa()));
        install.Set(L"moduleGeneration", Json::Int(preservingModules ? current->ModuleGeneration() : kModuleGeneration));
        install.Set(L"installedUtc", Json::Str(UtcNowIso()));
        install.Set(L"files", std::move(files));
        SaveJson(PathCombine(app, L"install.json"), install);
    }

    // Load each compiled module and check its exports before switching.
    void ValidateApp(const wstring& app) {
        std::vector<wstring> required = kModuleFiles;
        for (const wchar_t* name : {L"NebulaRuntime.exe", L"Nebula.exe", L"NebulaSettings.exe"}) required.push_back(name);
        for (const auto& name : required)
            if (!FileExists(PathCombine(app, name))) Fail(ErrorKind::InvalidOperation, L"The new version is missing " + name + L".");
        std::vector<std::pair<wstring, std::vector<const char*>>> exports = {
            {L"RMGE01_game.dll", {"galaxy_module_manifest", "galaxy_module_init", "galaxy_module_entry", "galaxy_lookup_function"}},
            {L"RMGE01_home_button.dll", {"galaxy_home_button_rso_manifest", "galaxy_home_button_rso_try_call", "galaxy_home_button_rso_resume"}},
            {L"RMGE01_dsp.dll", {"galaxy_rmge01_dsp_entry", "galaxy_rmge01_dsp_entry_expected_iram_sha1"}}};
        SetDllDirectoryW(app.c_str());
        struct Reset { ~Reset() { SetDllDirectoryW(nullptr); } } reset;
        for (const auto& pair : exports) {
            HMODULE module = LoadLibraryExW(PathCombine(app, pair.first).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!module) Fail(ErrorKind::InvalidOperation, pair.first + L" could not be loaded (error " + Num(GetLastError()) + L").");
            for (const char* name : pair.second) {
                if (!GetProcAddress(module, name)) {
                    FreeLibrary(module);
                    Fail(ErrorKind::InvalidOperation, pair.first + L" has no " + Widen(name) + L" export.");
                }
            }
            FreeLibrary(module);
        }
        Log(L"Validated the compiled modules.");
    }

    // An update installs beside the running version; Nebula must close only when this install replaces the folder it
    // runs from (a repair) or the game files the game reads. Setup asks and waits; unattended, it fails.
    void WaitUntilClosed(bool replacingVersion, bool replacingGameFiles) {
        for (;;) {
            std::optional<wstring> running;
            if (replacingVersion) running = RunningFrom(layout.VersionDir(build::kVersion), {L"NebulaRuntime", L"Nebula", L"NebulaSettings"});
            if (!running && replacingGameFiles) running = RunningFrom(layout.root, {L"NebulaRuntime"});
            if (!running) return;
            Log(L"Waiting for " + *running + L" to close.");
            if (!progress.AskToClose(L"Nebula is running (" + *running + L"). Close the game, the Nebula launcher and Nebula Settings, then choose "
                                     L"Retry to finish installing Nebula " + wstring(build::kVersion) + L"."))
                Fail(ErrorKind::InvalidOperation, L"Nebula is running. Close the game, the Nebula launcher and Nebula Settings, then try again.");
        }
    }

    // Puts fresh (built in staging) at destination; what is there is renamed aside first and renamed back if the new
    // tree cannot be moved in.
    void Swap(const wstring& fresh, const wstring& destination, const wstring& aside) {
        bool existed = DirExists(destination);
        if (existed) MoveDirectoryInPlace(destination, aside);
        try {
            MoveDirectory(fresh, destination, [this](const wstring& l) { Log(l); });
        } catch (...) {
            if (existed) {
                try {
                    MoveDirectoryInPlace(aside, destination);
                } catch (const Error& error) {
                    Log(L"Could not put " + destination + L" back from " + aside + L": " + error.message);
                }
            }
            throw;
        }
    }

    // The Start menu's Update and Setup shortcuts run this copy. A held file is retried, then logged.
    void CopySetup(const wstring& self) {
        for (int attempt = 0;; attempt++) {
            try {
                CopyFileOrThrow(self, layout.SetupCopy(), true);
                return;
            } catch (const Error& error) {
                if (error.kind != ErrorKind::Io && error.kind != ErrorKind::Access) throw;
                if (attempt >= 20) {
                    Log(L"Could not copy Setup to " + layout.SetupCopy() + L": " + error.message);
                    return;
                }
                Sleep(500);
            }
        }
    }

    void Commit(const wstring& app, const std::optional<InstalledInfo>& current) {
        wstring version = build::kVersion;
        wstring versionDir = layout.VersionDir(version);
        WaitUntilClosed(DirExists(versionDir), DirExists(PathCombine(staging, L"content")) || DirExists(PathCombine(staging, L"game-inputs")));
        CreateDirs(layout.Versions());
        wstring replaced = versionDir + L".replaced-" + NewGuidN().substr(0, 8);
        Swap(app, versionDir, replaced);

        wstring newContent = PathCombine(staging, L"content");
        if (DirExists(newContent)) {
            Json marker = Json::Object();
            marker.Set(L"formatVersion", Json::Int(kContentFormatVersion));
            marker.Set(L"createdUtc", Json::Str(UtcNowIso()));
            SaveJson(PathCombine(newContent, L"content.json"), marker);
            Swap(newContent, layout.Content(), PathCombine(staging, L"content-old"));
        }
        wstring newInputs = PathCombine(staging, L"game-inputs");
        if (DirExists(newInputs)) Swap(newInputs, layout.GameInputs(), PathCombine(staging, L"game-inputs-old"));

        std::optional<wstring> previous = current && current->Version != version ? std::optional<wstring>(current->Version)
                                                                                 : (current ? current->Previous : std::nullopt);
        Json record = Json::Object();
        record.Set(L"version", Json::Str(version));
        record.Set(L"previous", previous ? Json::Str(*previous) : Json::Null());
        SaveJson(layout.Current(), record);
        // Nebula is installed from here on: what follows is tidying and shortcuts, and does not fail the install. One
        // previous version is kept for rollback; a folder still held is removed by a later install.
        for (const auto& dir : ListDirs(layout.Versions(), false)) {
            wstring name = FileName(dir);
            if (name != version && (!previous || name != *previous)) {
                try {
                    DeleteTree(dir);
                } catch (const Error& error) {
                    Log(L"Could not remove " + dir + L": " + error.message);
                }
            }
        }
        wstring self = ModulePath();
        if (!IEquals(FullPath(self), layout.SetupCopy())) CopySetup(self);
        for (const auto& failure : integration::Register(layout, version, request.DesktopShortcut))
            Log(L"Could not create the shortcut " + failure + L"; Nebula.exe is in " + versionDir + L".");
        owner.InstalledVersionDir = versionDir;
        Log(L"Installed " + version + L" to " + versionDir + (previous ? L"; kept " + *previous + L" for rollback." : wstring(L".")));
    }

    void CleanAbandonedStaging() {
        for (const auto& dir : ListDirs(layout.Staging(), false)) {
            try {
                DeleteTree(dir);
                Log(L"Removed unfinished work: " + dir);
            } catch (const Error&) {
            }
        }
    }

    void Run() {
        Log(L"Nebula Setup " + wstring(build::kVersion) + L" (" + build::kCommit + L"), " +
            (request.Mode == InstallMode::Install ? L"Install" : request.Mode == InstallMode::Repair ? L"Repair" : L"Update"));
        Log(L"Install folder: " + layout.root);
        if (LowMemoryWarning())
            Log(L"RAM: " + FormatBytes((long long)TotalPhysicalMemory()) + L", below the 8 GB Nebula is designed for; installing as experimental.");
        CreateDirs(layout.root);
        CreateDirs(layout.Staging());
        CleanAbandonedStaging();
        staging = PathCombine(layout.Staging(), UtcStamp(false) + L"-" + NewGuidN().substr(0, 8));
        CreateDirs(staging);
        std::thread prepare, dlssThread;
        std::exception_ptr prepareFailure;
        CancelToken prepareCancel;
        std::atomic<bool> prepareDone{false};
        // Linked: canceling Setup cancels the background work.
        std::thread link([&] {
            while (!prepareDone) {
                if (cancel.IsCanceled()) prepareCancel.Cancel();
                Sleep(100);
            }
        });
        std::optional<wstring> src;
        // DLSS lives in the user data folder and downloads while the game is built; a failure does not fail the install.
        std::atomic<bool> dlssRunning{false};
        if (request.InstallDlss) {
            dlssRunning = true;
            dlssThread = std::thread([&] {
                try {
                    dlssdownload::Install([&](long long done, long long total) {
                        if (waitingForDlss) StepProgress(std::nullopt, total > 0 ? double(done) / double(total) : -1);
                    }, prepareCancel);
                    Log(L"NVIDIA DLSS installed in " + dlss::Folder());
                } catch (const std::exception& error) {
                    if (!IsKind(error, ErrorKind::Canceled)) {
                        owner.DlssFailure = ErrorText(error);
                        Log(L"NVIDIA DLSS download failed: " + ErrorText(error));
                    }
                }
                dlssRunning = false;
            });
        }
        auto finish = [&] {
            if (prepare.joinable()) prepare.join();
            if (dlssThread.joinable()) dlssThread.join();
            prepareDone = true;
            link.join();
            try {
                DeleteTree(staging);
            } catch (const Error& error) {
                Log(L"Could not remove staging: " + error.message);
            }
        };
        try {
            auto current = InstalledInfo::Read(layout);
            preservingModules = current && request.Mode == InstallMode::Update;
            if (preservingModules) {
                preservingModules = !request.GameInput && current->FilesIntact(kModuleFiles) && ContentValid(layout);
                if (preservingModules) {
                    try {
                        CheckModuleVersions(current->Directory, PayloadJson(L"module-compatibility.json"));
                    } catch (const Error& error) {
                        if (error.kind == ErrorKind::InvalidData) {
                            preservingModules = false;
                            Log(L"Module validation requires rebuild fallback: " + error.message);
                        } else if (error.kind == ErrorKind::InvalidOperation) {
                            preservingModules = false;
                            Log(L"Module ABI requires rebuild fallback: " + error.message);
                        } else {
                            throw;
                        }
                    }
                }
                if (preservingModules && current->ModuleGeneration() < kModuleGeneration) {
                    preservingModules = false;
                    Log(L"The installed " + current->ModuleIsa() + L" game modules are generation " + Num(current->ModuleGeneration()) +
                        L"; this release needs " + Num(kModuleGeneration) + L" (a relink cannot update their code). Rebuilding them.");
                }
                if (preservingModules && current->ModuleKey() != ModuleKey() &&
                    !FileExists(PathCombine(current->Directory, L"relink-cache", L"manifest.json"))) {
                    preservingModules = false;
                    Log(L"No retained objects for changed module dependencies. Rebuilding once as the last resort; this install will retain objects for later relinks.");
                }
            }
            bool needInput = !preservingModules && (request.GameInput || !RetainedInputsValid(layout) || !ContentValid(layout));
            if (needInput && !request.GameInput) Fail(ErrorKind::InvalidOperation, L"Choose your Super Mario Galaxy ISO, RVZ or extracted folder.");

            bool reuseModules = preservingModules;
            Log(wstring(L"Reuse compiled modules: ") + (reuseModules ? L"True" : L"False"));

            // Downloads run while the game is extracted and recompiled.
            if (!reuseModules) {
                if (!toolchain.IsReady()) Log(L"Microsoft C++ Build Tools license: " + toolchain.LicenseUrl());
                prepare = std::thread([&] {
                    try {
                        toolchain.Ensure([this](const wstring& t, double f) { BackgroundProgress(t, f); }, [this](const wstring& l) { Log(l); },
                                         prepareCancel);
                        src = source.Ensure(layout.Sources(), request.SourceOverride, [this](const wstring& t, double f) { BackgroundProgress(t, f); },
                                            [this](const wstring& l) { Log(l); }, prepareCancel);
                    } catch (...) {
                        prepareFailure = std::current_exception();
                    }
                });
            }

            if (request.GameInput) {
                Stage(0.00, 0.02, L"Checking your game");
                Identify(*request.GameInput);
                wstring data = PrepareData(*request.GameInput);
                CopyGameInputs(data, PathCombine(staging, L"game-inputs"));
                Stage(0.10, 0.08, L"Packing the game files");
                RunRecomp({L"package-content", data, L"--output", PathCombine(staging, L"content")}, nullptr);
            }

            wstring app = PathCombine(staging, L"app");
            CreateDirs(app);
            if (reuseModules) {
                Stage(0.20, 0.70, L"Reusing the compiled game");
                current->CopyFiles(app, kModuleFiles);
                current->CopyFiles(app, current->RuntimeDllNames());
                wstring patchLibraries = PathCombine(staging, L"runtime-libs");
                for (const auto& lib : kRuntimeLibraries) payload::Extract(RuntimePayload(lib), PathCombine(patchLibraries, lib));
                try {
                    bool linked = relink::Update(current->Directory, app, patchLibraries, toolchain.Id(),
                                                 [&](const std::vector<wstring>& args) {
                                                     if (!toolchain.IsReady())
                                                         toolchain.Ensure([this](const wstring& t, double f) { BackgroundProgress(t, f); },
                                                                          [this](const wstring& l) { Log(l); }, cancel);
                                                     Environment env = toolchain.BuildEnvironment();
                                                     return RunTool(toolchain.LldLink(), args, &env, [this](const wstring& l) { Log(l); });
                                                 },
                                                 [this](const wstring& l) { Log(l); });
                    if (!linked) {
                        owner.ModuleUpdateNotice = L"Nebula was updated. The game modules from the previous version were kept.";
                        Log(*owner.ModuleUpdateNotice);
                    }
                    CheckModuleVersions(app, PayloadJson(L"module-compatibility.json"));
                } catch (const Error& error) {
                    if (error.kind == ErrorKind::InvalidData) {
                        Log(L"Retained object validation failed: " + error.message);
                        RebuildForUpdate(app);
                    } else if (error.kind == ErrorKind::InvalidOperation) {
                        Log(L"Relink path unavailable: " + error.message);
                        RebuildForUpdate(app);
                    } else {
                        throw;
                    }
                }
            } else {
                Stage(0.18, 0.04, L"Recompiling the game code");
                wstring inputs = request.GameInput ? PathCombine(staging, L"game-inputs") : layout.GameInputs();
                wstring gen = PathCombine(staging, L"generated");
                RunRecomp({L"generate", inputs, L"--output", gen}, nullptr);
                Stage(0.22, 0.08, L"Preparing the compiler");
                waitingForDownloads = true;
                prepare.join();
                waitingForDownloads = false;
                if (prepareFailure) std::rethrow_exception(prepareFailure);
                Build(*src, gen, app);
            }

            if (dlssThread.joinable() && dlssRunning) {
                Stage(0.92, 0.02, L"Downloading NVIDIA DLSS");
                waitingForDlss = true;
                while (dlssRunning) {
                    cancel.ThrowIfCanceled();
                    Sleep(200);
                }
                waitingForDlss = false;
            }
            Stage(0.94, 0.02, L"Finishing");
            AssembleApp(app, current);
            SeedShaderCache();
            ValidateApp(app);
            Stage(0.96, 0.04, L"Switching to the new version");
            Commit(app, current);
            Report(1.0, L"Nebula " + wstring(build::kVersion) + L" is installed.", 1);
            Log(L"Completed in " + ElapsedMinutes(GetTickCount64() - started) + L".");
        } catch (const std::exception& error) {
            prepareCancel.Cancel();
            Log(L"FAILED: " + ErrorText(error));
            finish();
            throw;
        }
        finish();
    }
};

InstallEngine::InstallEngine(InstallRequest request, Progress& progress, CancelToken& cancel)
    : impl_(std::make_unique<Impl>(std::move(request), progress, cancel, *this)) {}
InstallEngine::~InstallEngine() = default;
void InstallEngine::Run() { impl_->Run(); }

// ---- integration -------------------------------------------------------------------------------------------------

namespace integration {
static const wchar_t* const kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Nebula";
static const wchar_t* const kRemoverPrefix = L"Nebula-Uninstall-";

static wstring StartMenuFolder() { return PathCombine(KnownFolder(FOLDERID_Programs), L"Nebula"); }
static wstring DesktopLink() { return PathCombine(KnownFolder(FOLDERID_Desktop), L"Nebula.lnk"); }

static void Shortcut(const wstring& path, const wstring& target, const wstring& arguments, const wstring& workingDirectory,
                     const wstring& description) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr)) Fail(ErrorKind::InvalidOperation, L"Shortcuts are not available (" + Num(hr) + L").");
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
    if (FAILED(hr)) Fail(ErrorKind::Io, L"The shortcut could not be saved (" + Num(hr) + L").");
}

static void SetString(HKEY key, const wchar_t* name, const wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
}

// Shortcuts, the install-root key and the Add/Remove Programs entry. A shortcut that cannot be written is returned as
// a failure, not thrown: Nebula runs without it, and the registry entries still follow.
std::vector<wstring> Register(const InstallLayout& layout, const wstring& version, bool desktop) {
    wstring app = layout.VersionDir(version);
    std::vector<wstring> failures;
    auto link = [&](const wstring& path, const wstring& target, const wstring& arguments, const wstring& directory, const wstring& description) {
        try {
            Shortcut(path, target, arguments, directory, description);
        } catch (const std::exception& error) {
            failures.push_back(FileName(path) + L": " + ErrorText(error));
        }
    };
    CreateDirs(StartMenuFolder());
    link(PathCombine(StartMenuFolder(), L"Nebula.lnk"), PathCombine(app, L"Nebula.exe"), L"", app, L"Play Nebula");
    link(PathCombine(StartMenuFolder(), L"Nebula Settings.lnk"), PathCombine(app, L"NebulaSettings.exe"), L"", app,
         L"Display, graphics, audio and game files");
    link(PathCombine(StartMenuFolder(), L"Update Nebula.lnk"), layout.SetupCopy(), L"--update", layout.root, L"Check for and install Nebula updates");
    link(PathCombine(StartMenuFolder(), L"Nebula Setup.lnk"), layout.SetupCopy(), L"", layout.root, L"Repair, restore or uninstall Nebula");
    if (desktop || FileExists(DesktopLink())) link(DesktopLink(), PathCombine(app, L"Nebula.exe"), L"", app, L"Play Nebula");

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Nebula", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        SetString(key, L"InstallRoot", layout.root);
        RegCloseKey(key);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        SetString(key, L"DisplayName", L"Nebula");
        SetString(key, L"DisplayVersion", version);
        SetString(key, L"Publisher", L"keepvaibin");
        SetString(key, L"InstallLocation", layout.root);
        SetString(key, L"DisplayIcon", PathCombine(app, L"Nebula.exe"));
        SetString(key, L"UninstallString", L"\"" + layout.SetupCopy() + L"\" --uninstall");
        SetString(key, L"ModifyPath", L"\"" + layout.SetupCopy() + L"\"");
        SetString(key, L"URLInfoAbout", L"https://github.com/" + wstring(build::kRepository));
        DWORD noRepair = 0;
        RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&noRepair), sizeof(noRepair));
        RegCloseKey(key);
    }
    return failures;
}

void RequireClosed(const wstring& root) {
    if (RunningFrom(root, {L"NebulaRuntime", L"Nebula", L"NebulaSettings"}))
        Fail(ErrorKind::InvalidOperation, L"Nebula is running. Close the game, the Nebula launcher and Nebula Settings, then try again.");
}

// Switch back to the retained previous version.
wstring Rollback(const InstallLayout& layout) {
    RequireClosed(layout.root);
    auto current = InstalledInfo::Read(layout);
    if (!current || !current->Previous) Fail(ErrorKind::InvalidOperation, L"No previous version is available.");
    Json record = Json::Object();
    record.Set(L"version", Json::Str(*current->Previous));
    record.Set(L"previous", Json::Str(current->Version));
    SaveJson(layout.Current(), record);
    Register(layout, *current->Previous, false);
    return *current->Previous;
}

// Remove the program; saves, settings and caches stay unless asked.
void Uninstall(const InstallLayout& layout, bool removeUserData) {
    RequireClosed(layout.root);
    // Files first: if one is held, the Add/Remove Programs entry and shortcuts are still there to try again.
    wstring self = FullPath(ModulePath());
    for (const auto& entry : ListEntries(layout.root)) {
        if (IEquals(FullPath(entry), self)) continue;
        try {
            if (DirExists(entry)) DeleteTree(entry);
            else DeleteFileOrThrow(entry);
        } catch (const Error& error) {
            if (error.kind != ErrorKind::Io && error.kind != ErrorKind::Access) throw;
            Fail(ErrorKind::Io, L"Could not remove " + entry + L": " + error.message + kHeldAdvice);
        }
    }
    if (DirExists(StartMenuFolder())) DeleteTree(StartMenuFolder());
    if (FileExists(DesktopLink())) DeleteFileOrThrow(DesktopLink());
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Nebula");
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Nebula");
    if (removeUserData && DirExists(paths::DataRoot())) DeleteTree(paths::DataRoot());
    // A running program cannot delete its own file. When this is the Setup copy in the install folder, a copy of it in
    // the temporary folder removes it and the emptied folder once this process has ended, as NSIS and Inno Setup
    // uninstallers do.
    if (StartsWith(self, FullPath(layout.root), true)) {
        wstring folder = TempDirectory() + kRemoverPrefix + NewGuidN();
        CreateDirs(folder);
        wstring remover = PathCombine(folder, FileName(self));
        CopyFileOrThrow(self, remover, false);
        StartProgram(remover, L"--finish-uninstall " + Num(GetCurrentProcessId()) + L" --root " + QuoteArg(layout.root), folder);
    } else if (DirExists(layout.root) && IsDirectoryEmpty(layout.root)) {
        RemoveEmptyDirectory(layout.root);
    }
}

// The temporary copy's part of an uninstall: once Setup has ended, its copy in the install folder and the emptied
// folder are removed, and so are the temporary copies of earlier uninstalls.
void FinishUninstall(DWORD setupProcess, const wstring& root) {
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
    try {
        if (DirExists(layout.root) && IsDirectoryEmpty(layout.root)) RemoveEmptyDirectory(layout.root);
    } catch (const Error&) {
    }
    wstring own = DirName(FullPath(ModulePath()));
    for (const auto& folder : ListDirs(TempDirectory(), false)) {
        if (!StartsWith(FileName(folder), kRemoverPrefix, true) || IEquals(FullPath(folder), own)) continue;
        try { DeleteTree(folder); } catch (const Error&) {}
    }
}
}  // namespace integration

// ---- updates -----------------------------------------------------------------------------------------------------

namespace updater {
static bool IsBetaVersion(const wstring& version) { return IFind(version, L"-beta.") != wstring::npos; }

// Betas require an explicit first download; separate asset names keep older updaters from offering a beta.
static std::optional<ReleaseInfo> SelectNewer(const wstring& json, const wstring& installedVersion) {
    bool beta = IsBetaVersion(installedVersion);
    wstring setupName = beta ? L"Nebula-Beta-Setup.exe" : L"Nebula-Setup.exe";
    std::optional<ReleaseInfo> best;
    Json releases = ParseJson(json);
    for (const auto& release : releases.items) {
        if (JBool(&release, L"draft")) continue;
        wstring tag = JStr(&release, L"tag_name").value_or(L"");
        wstring version = StartsWith(tag, L"v") ? tag.substr(1) : tag;
        if (IsBetaVersion(version) != beta) continue;
        if (CompareVersions(version, best ? best->Version : installedVersion) <= 0) continue;
        std::optional<wstring> setup, signature;
        if (const Json* assets = JList(&release, L"assets")) {
            for (const auto& asset : assets->items) {
                wstring name = JStr(&asset, L"name").value_or(L"");
                if (name == setupName) setup = JStr(&asset, L"browser_download_url");
                if (name == setupName + L".sig") signature = JStr(&asset, L"browser_download_url");
            }
        }
        if (!setup || !signature) continue;
        ReleaseInfo info;
        info.Version = version;
        info.Tag = tag;
        info.Notes = JStr(&release, L"body").value_or(L"");
        info.SetupUrl = *setup;
        info.SignatureUrl = *signature;
        info.Prerelease = JBool(&release, L"prerelease");
        best = info;
    }
    return best;
}

std::optional<ReleaseInfo> FindNewer() {
    std::string json = http::GetString(L"https://api.github.com/repos/" + wstring(build::kRepository) + L"/releases?per_page=30",
                                       L"application/vnd.github+json");
    return SelectNewer(Widen(json), build::kVersion);
}

wstring Download(const ReleaseInfo& release, const wstring& directory, const CancelToken& cancel) {
    wstring folder = PathCombine(directory, L"update-" + release.Version);
    wstring exe = PathCombine(folder, L"Nebula-Setup.exe");
    wstring sig = exe + L".sig";
    if (FileExists(exe)) DeleteFileOrThrow(exe);
    http::Fetch(release.SignatureUrl, sig, L"", 0, nullptr, cancel);
    http::Fetch(release.SetupUrl, exe, L"", 0, nullptr, cancel);
    bool valid = false;
    try {
        valid = VerifyRsaSha256File(exe, Base64Decode(Narrow(Trim(ReadAllText(sig)))), build::kReleasePublicKeyXml);
    } catch (const Error& error) {
        if (error.kind != ErrorKind::InvalidData) throw;
    }
    if (!valid) {
        DeleteFileOrThrow(exe);
        Fail(ErrorKind::InvalidData, L"The downloaded Nebula " + release.Version +
                                         L" setup is not signed by the Nebula release key. It was deleted and not run.");
    }
    return exe;
}
}  // namespace updater

// ---- DLSS --------------------------------------------------------------------------------------------------------

namespace dlssdownload {
const long long kDownloadBytes = 32003270;
static const wchar_t* const kArchiveUrl = L"https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.14.1/streamline-sdk-v2.14.1.zip";
static const wchar_t* const kArchiveSha256 = L"92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b";
static const long long kArchiveSize = 275994000;
static const wchar_t* const kAgent = L"Nebula";

namespace {
struct RangesUnsupported : Error {
    RangesUnsupported() : Error(ErrorKind::Web, L"The server does not support partial downloads.") {}
};

struct RemoteEntry {
    std::string name;
    int method = 0;
    long long compressed = 0, size = 0, offset = 0;
};

uint32_t U16(const uint8_t* b) { return uint32_t(b[0] | b[1] << 8); }
uint32_t U32(const uint8_t* b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }

bool Retryable(const Error& error) { return error.kind == ErrorKind::Web || error.kind == ErrorKind::Io || error.kind == ErrorKind::InvalidData; }

// Parts of a zip on a web server, read with HTTP range requests.
class RemoteZip {
public:
    RemoteZip(wstring url, long long size) : url_(std::move(url)), size_(size) {}

    std::map<std::string, RemoteEntry> Directory(const CancelToken& cancel) {
        int tailLength = int(std::min<long long>(size_, 65536 + 22));
        Bytes tail = Read(size_ - tailLength, tailLength, cancel);
        int end = -1;
        for (int i = int(tail.size()) - 22; i >= 0; i--)
            if (U32(&tail[size_t(i)]) == 0x06054b50u) { end = i; break; }
        if (end < 0) Fail(ErrorKind::InvalidData, L"The Streamline release zip has no directory.");
        long long directorySize = U32(&tail[size_t(end) + 12]), directoryOffset = U32(&tail[size_t(end) + 16]);
        if (directoryOffset + directorySize > size_) Fail(ErrorKind::InvalidData, L"The Streamline release zip directory is invalid.");
        Bytes directory = Read(directoryOffset, int(directorySize), cancel);
        std::map<std::string, RemoteEntry> entries;
        for (size_t p = 0; p + 46 <= directory.size() && U32(&directory[p]) == 0x02014b50u;) {
            size_t nameLength = U16(&directory[p + 28]);
            RemoteEntry entry;
            entry.method = int(U16(&directory[p + 10]));
            entry.compressed = U32(&directory[p + 20]);
            entry.size = U32(&directory[p + 24]);
            entry.offset = U32(&directory[p + 42]);
            entry.name = std::string(reinterpret_cast<const char*>(&directory[p + 46]), nameLength);
            entries[entry.name] = entry;
            p += 46 + nameLength + U16(&directory[p + 30]) + U16(&directory[p + 32]);
        }
        return entries;
    }

    // Writes the entry's uncompressed bytes; onRead gets the compressed bytes read so far.
    void Extract(const RemoteEntry& entry, const std::function<void(const uint8_t*, size_t)>& output, const std::function<void()>& restart,
                 const std::function<void(long long)>& onRead, const CancelToken& cancel) {
        if (entry.method != 0 && entry.method != 8) Fail(ErrorKind::InvalidData, Widen(entry.name) + L" uses an unsupported compression method.");
        Bytes header = Read(entry.offset, 30, cancel);
        if (U32(header.data()) != 0x04034b50u) Fail(ErrorKind::InvalidData, Widen(entry.name) + L" has no local header.");
        long long data = entry.offset + 30 + U16(&header[26]) + U16(&header[28]);
        if (entry.compressed == 0) return;
        for (int attempt = 1;; attempt++) {
            try {
                auto request = Open(data, entry.compressed);
                long long count = 0;
                Inflater inflater;
                std::vector<uint8_t> buffer(1 << 16);
                for (size_t read; (read = request->Read(buffer.data(), buffer.size())) > 0;) {
                    cancel.ThrowIfCanceled();
                    count += long long(read);
                    if (onRead) onRead(count);
                    if (entry.method == 8) inflater.Feed(buffer.data(), read, output);
                    else output(buffer.data(), read);
                }
                if (count != entry.compressed) Fail(ErrorKind::Io, Widen(entry.name) + L" was cut short.");
                return;
            } catch (const RangesUnsupported&) {
                throw;
            } catch (const Error& error) {
                if (error.kind == ErrorKind::Canceled || !Retryable(error) || attempt >= 3 || cancel.IsCanceled()) throw;
                restart();
                resolved_.clear();
                Sleep(DWORD(1500 * attempt));
            }
        }
    }

    Bytes Read(long long offset, int count, const CancelToken& cancel) {
        for (int attempt = 1;; attempt++) {
            try {
                auto request = Open(offset, count);
                Bytes buffer(static_cast<size_t>(count));
                size_t filled = 0;
                for (size_t read; filled < buffer.size() && (read = request->Read(buffer.data() + filled, buffer.size() - filled)) > 0;) {
                    cancel.ThrowIfCanceled();
                    filled += read;
                }
                if (filled != buffer.size()) Fail(ErrorKind::Io, L"The download was cut short.");
                return buffer;
            } catch (const RangesUnsupported&) {
                throw;
            } catch (const Error& error) {
                if (error.kind == ErrorKind::Canceled || (error.kind != ErrorKind::Web && error.kind != ErrorKind::Io) || attempt >= 3 ||
                    cancel.IsCanceled())
                    throw;
                resolved_.clear();
                Sleep(DWORD(1500 * attempt));
            }
        }
    }

    static void Download(const wstring& url, const std::function<void(const uint8_t*, size_t)>& output,
                         const std::function<void(long long)>& onRead, const CancelToken& cancel) {
        HttpRequest request(url, L"GET", kAgent, true, 60000);
        request.Send();
        if (request.Status() >= 400) http::FailStatus(url, request.Status());
        long long count = 0;
        std::vector<uint8_t> buffer(1 << 20);
        for (size_t read; (read = request.Read(buffer.data(), buffer.size())) > 0;) {
            cancel.ThrowIfCanceled();
            count += long long(read);
            output(buffer.data(), read);
            if (onRead) onRead(count);
        }
    }

private:
    std::unique_ptr<HttpRequest> Open(long long offset, long long count) {
        if (resolved_.empty()) resolved_ = Resolve(url_);
        auto request = std::make_unique<HttpRequest>(resolved_, L"GET", kAgent, false, 30000);
        request->AddHeader(L"Range: bytes=" + Num(offset) + L"-" + Num(offset + count - 1));
        request->Send();
        if (request->Status() != 206) {
            if (request->Status() >= 400) http::FailStatus(resolved_, request->Status());
            throw RangesUnsupported();
        }
        return request;
    }

    // GitHub redirects to a signed storage address; the ranges go there directly.
    static wstring Resolve(const wstring& start) {
        wstring current = start;
        for (int hop = 0; hop < 5; hop++) {
            HttpRequest request(current, L"HEAD", kAgent, false, 30000);
            request.Send();
            int status = request.Status();
            auto location = request.Header(L"Location");
            if (status < 300 || status >= 400 || !location || location->empty()) return current;
            if (StartsWith(*location, L"/")) {
                size_t scheme = current.find(L"://");
                size_t pathStart = current.find(L'/', scheme + 3);
                current = current.substr(0, pathStart) + *location;
            } else {
                current = *location;
            }
        }
        return current;
    }

    wstring url_;
    long long size_;
    wstring resolved_;
};

void Accept(const wstring& partial, const wstring& target, const dlss::Part& part) {
    wstring actual = Sha256File(partial);
    if (actual != part.sha256)
        Fail(ErrorKind::InvalidData, wstring(part.file) + L" failed its integrity check (expected SHA-256 " + part.sha256 + L", received " + actual + L").");
    if (FileExists(target)) DeleteFileOrThrow(target);
    MoveFileOrThrow(partial, target);
}

struct OutFile {
    HANDLE h;
    wstring path;
    explicit OutFile(const wstring& p) : path(p) {
        h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), p);
    }
    ~OutFile() { Close(); }
    void Close() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    void Write(const uint8_t* data, size_t size) {
        DWORD written = 0;
        if (!WriteFile(h, data, DWORD(size), &written, nullptr)) FailWin32(GetLastError(), path);
    }
    void Truncate() {
        LARGE_INTEGER zero{};
        SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
        SetEndOfFile(h);
    }
};

// For connections that drop range requests: the whole release zip.
void InstallFromArchive(const std::function<void(long long, long long)>& progress, const CancelToken& cancel) {
    wstring archive = PathCombine(dlss::Folder(), L"streamline-sdk-v2.14.1.zip.partial");
    try {
        {
            OutFile output(archive);
            RemoteZip::Download(kArchiveUrl, [&](const uint8_t* d, size_t n) { output.Write(d, n); },
                                [&](long long read) { if (progress) progress(read, kArchiveSize); }, cancel);
        }
        if (Sha256File(archive) != kArchiveSha256) Fail(ErrorKind::InvalidData, L"The Streamline release zip failed its integrity check.");
        auto zip = ZipReader::OpenFile(archive);
        for (const auto& part : dlss::kParts) {
            const ZipReader::Entry* entry = zip->Find(part.entry);
            if (!entry) Fail(ErrorKind::InvalidData, L"The Streamline release has no " + Widen(part.entry) + L".");
            wstring target = PathCombine(dlss::Folder(), part.file), partial = target + L".partial";
            try {
                zip->ExtractTo(*entry, partial, true);
                Accept(partial, target, part);
            } catch (...) {
                DeleteFileQuietly(partial);
                throw;
            }
            DeleteFileQuietly(partial);
        }
    } catch (...) {
        DeleteFileQuietly(archive);
        throw;
    }
    DeleteFileQuietly(archive);
}
}  // namespace

// NVIDIA's DLSS license (NVIDIA RTX SDKs license), from the install or the release zip.
wstring LicenseText(const CancelToken& cancel) {
    wstring local = PathCombine(dlss::Folder(), L"nvngx_dlss.license.txt");
    if (FileExists(local)) return ReadAllText(local);
    RemoteZip zip(kArchiveUrl, kArchiveSize);
    auto entries = zip.Directory(cancel);
    auto it = entries.find(dlss::kLicenseEntry);
    if (it == entries.end()) Fail(ErrorKind::InvalidData, L"The Streamline release has no DLSS license.");
    std::string text;
    zip.Extract(it->second, [&](const uint8_t* d, size_t n) { text.append(reinterpret_cast<const char*>(d), n); }, [&] { text.clear(); },
                nullptr, cancel);
    return Widen(text);
}

// Downloads the missing files into the DLSS folder; progress gets (bytes read, bytes to read).
void Install(const std::function<void(long long, long long)>& progress, const CancelToken& cancel) {
    CreateDirs(dlss::Folder());
    std::vector<const dlss::Part*> missing;
    long long total = 0;
    for (const auto& part : dlss::kParts) {
        wstring path = PathCombine(dlss::Folder(), part.file);
        if (FileExists(path) && FileSize(path) == part.size && Sha256File(path) == part.sha256) continue;
        missing.push_back(&part);
        total += part.compressed;
    }
    if (missing.empty()) return;
    if (progress) progress(0, total);
    RemoteZip zip(kArchiveUrl, kArchiveSize);
    std::map<std::string, RemoteEntry> entries;
    try {
        entries = zip.Directory(cancel);
    } catch (const RangesUnsupported&) {
        InstallFromArchive(progress, cancel);
        return;
    }
    long long done = 0;
    for (const auto* part : missing) {
        auto it = entries.find(part->entry);
        if (it == entries.end() || it->second.size != part->size)
            Fail(ErrorKind::InvalidData, L"The Streamline release on GitHub changed: " + Widen(part->entry) + L" is missing or has another size.");
        long long before = done;
        wstring target = PathCombine(dlss::Folder(), part->file), partial = target + L".partial";
        try {
            {
                OutFile output(partial);
                zip.Extract(it->second, [&](const uint8_t* d, size_t n) { output.Write(d, n); }, [&] { output.Truncate(); },
                            [&](long long read) { if (progress) progress(before + read, total); }, cancel);
            }
            Accept(partial, target, *part);
        } catch (...) {
            DeleteFileQuietly(partial);
            throw;
        }
        DeleteFileQuietly(partial);
        done = before + part->compressed;
    }
}
}  // namespace dlssdownload

}  // namespace nebula::setup
