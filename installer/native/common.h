// Shared by Nebula.exe, NebulaSettings.exe and Nebula-Setup.exe: errors, strings, files, JSON, hashes and the
// per-user locations. Everything is wide (UTF-16) inside; files are UTF-8.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "build_info.h"

namespace nebula {

using std::wstring;
using Bytes = std::vector<uint8_t>;

// ---- errors ------------------------------------------------------------------------------------------------------

enum class ErrorKind { Io, Access, InvalidData, InvalidOperation, Canceled, Web, NotFound, Argument };

struct Error : std::exception {
    ErrorKind kind;
    wstring message;
    DWORD win32 = 0;
    int http = 0;
    Error(ErrorKind k, wstring m, DWORD w = 0, int h = 0) : kind(k), message(std::move(m)), win32(w), http(h) {}
    const char* what() const noexcept override { return "nebula::Error"; }
};

[[noreturn]] void Fail(ErrorKind kind, const wstring& message);
// A Win32 failure: access denied reads as Access, everything else as Io. The text is Windows' own.
[[noreturn]] void FailWin32(DWORD code, const wstring& context);
wstring Win32Message(DWORD code);
// The message of any exception Nebula code can see.
wstring ErrorText(const std::exception& error);
bool IsKind(const std::exception& error, ErrorKind kind);
// ERROR_VIRUS_INFECTED, ERROR_VIRUS_DELETED: an antivirus refused the file.
bool BlockedByAntivirus(DWORD code);

// The user canceled; thrown by long operations at their next check.
struct CancelToken {
    std::atomic<bool> requested{false};
    void Cancel() { requested = true; }
    bool IsCanceled() const { return requested.load(); }
    void ThrowIfCanceled() const;
};

// ---- strings -----------------------------------------------------------------------------------------------------

wstring Widen(const std::string& utf8);
std::string Narrow(const wstring& text);
wstring ToLower(wstring text);
wstring ToUpper(wstring text);
bool IEquals(const wstring& a, const wstring& b);
bool StartsWith(const wstring& text, const wstring& prefix, bool ignoreCase = false);
bool EndsWith(const wstring& text, const wstring& suffix, bool ignoreCase = false);
size_t IFind(const wstring& text, const wstring& needle);
wstring Trim(const wstring& text);
wstring TrimEnd(const wstring& text, const wstring& characters);
wstring TrimStart(const wstring& text, const wstring& characters);
std::vector<wstring> Split(const wstring& text, wchar_t separator);
wstring Replace(wstring text, const wstring& from, const wstring& to);
wstring Join(const std::vector<wstring>& parts, const wstring& separator);
bool ParseInt(const wstring& text, long long& value);
bool ParseDouble(const wstring& text, double& value);
bool ParseFloat(const wstring& text, float& value);
wstring Num(long long value);
// The shortest text that reads back as the same float (C# float.ToString("R")).
wstring FloatR(float value);
wstring Fixed(double value, int decimals);
wstring HexLower(const uint8_t* data, size_t size);
wstring FormatBytes(long long bytes);

// ---- time and identifiers ----------------------------------------------------------------------------------------

wstring UtcNowIso();                         // 2026-10-29T08:00:00.1234567Z
wstring UtcStamp(bool milliseconds);         // yyyyMMdd-HHmmss[-fff]
wstring LocalStamp();                        // yyyyMMdd-HHmmss
wstring UtcClock();                          // HH:mm:ss.fff
wstring NewGuidN();                          // 32 lowercase hex digits

// ---- environment -------------------------------------------------------------------------------------------------

std::optional<wstring> GetEnv(const wstring& name);
wstring KnownFolder(const GUID& id);
wstring ModulePath();                        // this program's file
wstring ModuleDirectory();                   // its folder, without a trailing separator
wstring TempDirectory();                     // with a trailing separator

// ---- command lines -----------------------------------------------------------------------------------------------

wstring QuoteArg(const wstring& argument);   // CommandLineToArgvW quoting
wstring JoinArgs(const std::vector<wstring>& arguments);
std::vector<wstring> CommandLineArgs();      // without the program name

// ---- paths and files ---------------------------------------------------------------------------------------------

wstring PathCombine(const wstring& a, const wstring& b);
template <typename... Rest>
wstring PathCombine(const wstring& a, const wstring& b, const Rest&... rest) {
    return PathCombine(PathCombine(a, b), rest...);
}
wstring DirName(const wstring& path);
wstring FileName(const wstring& path);
wstring FullPath(const wstring& path);
wstring Slashes(wstring path);               // '/' to '\'
bool IsRooted(const wstring& path);
bool FileExists(const wstring& path);
bool DirExists(const wstring& path);
long long FileSize(const wstring& path);
bool IsReparsePoint(const wstring& path);
// Junctions and symbolic links resolved.
wstring FinalPath(const wstring& path);

void CreateDirs(const wstring& path);
void DeleteFileOrThrow(const wstring& path);
void DeleteFileQuietly(const wstring& path);
// Deletes a tree, clearing read-only attributes; retried for a moment while held.
void DeleteTree(const wstring& path);
void RemoveEmptyDirectory(const wstring& path);
std::vector<wstring> ListFiles(const wstring& directory, bool recursive);
std::vector<wstring> ListDirs(const wstring& directory, bool recursive);
std::vector<wstring> ListEntries(const wstring& directory);   // files and folders, one level
bool IsDirectoryEmpty(const wstring& directory);

Bytes ReadAllBytes(const wstring& path);
wstring ReadAllText(const wstring& path);    // UTF-8 (a BOM is dropped)
std::vector<wstring> ReadAllLines(const wstring& path);
void WriteAllBytes(const wstring& path, const void* data, size_t size, bool createNew = false, bool flush = false);
void WriteAllText(const wstring& path, const wstring& text);  // UTF-8 without a BOM
// Through a temporary file and an atomic replace.
void WriteAllTextAtomic(const wstring& path, const wstring& text);
void ReplaceFileWith(const wstring& temporary, const wstring& path);
void CopyFileOrThrow(const wstring& from, const wstring& to, bool overwrite);
void MoveFileOrThrow(const wstring& from, const wstring& to);

extern const wchar_t* const kHeldAdvice;
// Moves a directory, retried for about ten seconds while refused, then copied instead.
void MoveDirectory(const wstring& source, const wstring& destination, const std::function<void(const wstring&)>& log);
// The same without the copy (trees with folder streams); fails after the retries, nothing moved.
void MoveDirectoryInPlace(const wstring& source, const wstring& destination);
void CopyTree(const wstring& source, const wstring& destination);
// CopyFileW for every file (it carries their streams) plus the streams of every folder; links refused.
void CopyTreeWithStreams(wstring source, const wstring& target, const std::function<void(const wstring&)>& onFile);
void CopyNamedStreams(const wstring& source, const wstring& target);
std::map<wstring, wstring> StreamDigests(const wstring& path);
std::vector<std::pair<wstring, Bytes>> ReadNamedStreams(const wstring& path, long long maximumBytes);
void WriteNamedStream(const wstring& path, const wstring& name, const Bytes& bytes);

// ---- hashes ------------------------------------------------------------------------------------------------------

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void Update(const void* data, size_t size);
    wstring FinishHex();
    std::vector<uint8_t> Finish();

private:
    void* algorithm_ = nullptr;
    void* hash_ = nullptr;
};

wstring Sha256File(const wstring& path);
wstring Sha256Bytes(const void* data, size_t size);
wstring Sha256Text(const wstring& text);    // over its UTF-8
Bytes Base64Decode(const std::string& text);
std::string Base64Encode(const uint8_t* data, size_t size);
// RSA PKCS#1 v1.5 SHA-256 over a file, against an XML public key (<RSAKeyValue>).
bool VerifyRsaSha256File(const wstring& path, const Bytes& signature, const wstring& publicKeyXml);

// ---- JSON --------------------------------------------------------------------------------------------------------

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    bool integer = false;
    long long whole = 0;
    wstring text;
    std::vector<Json> items;
    std::vector<std::pair<wstring, Json>> members;

    Json() = default;
    static Json Null() { return Json(); }
    static Json Bool(bool v) { Json j; j.type = Type::Bool; j.boolean = v; return j; }
    static Json Int(long long v) { Json j; j.type = Type::Number; j.integer = true; j.whole = v; j.number = double(v); return j; }
    static Json Double(double v) { Json j; j.type = Type::Number; j.number = v; return j; }
    static Json Str(const wstring& v) { Json j; j.type = Type::String; j.text = v; return j; }
    static Json Array() { Json j; j.type = Type::Array; return j; }
    static Json Object() { Json j; j.type = Type::Object; return j; }

    bool IsNull() const { return type == Type::Null; }
    bool IsObject() const { return type == Type::Object; }
    bool IsArray() const { return type == Type::Array; }
    const Json* Find(const wstring& key) const;
    Json& Set(const wstring& key, Json value);    // replaces or appends (insertion order kept)
    void Add(Json value) { items.push_back(std::move(value)); }
};

Json ParseJson(const wstring& text);        // InvalidData on bad input
Json LoadJson(const wstring& path);
wstring SerializeJson(const Json& value);   // indented two spaces, as Nebula's C# tools wrote it
void SaveJson(const wstring& path, const Json& value);
// Json.Str / Long / Bool / Obj / List: a missing member or the wrong type reads as nothing.
std::optional<wstring> JStr(const Json* object, const wstring& key);
long long JLong(const Json* object, const wstring& key);
bool JBool(const Json* object, const wstring& key);
const Json* JObj(const Json* object, const wstring& key);
const Json* JList(const Json* object, const wstring& key);
wstring JScalarText(const Json& value);     // Convert.ToString of a scalar

// ---- Nebula's locations ------------------------------------------------------------------------------------------

namespace paths {
wstring LocalAppData();
wstring DefaultInstallRoot();                // %LOCALAPPDATA%\Programs\Nebula
wstring DataRoot();                          // %LOCALAPPDATA%\Nebula, or NEBULA_DATA_ROOT (tests)
wstring Saves();
wstring SettingsFile();
wstring Sessions();
wstring SetupLogs();
wstring ShaderCache();
std::optional<wstring> RegisteredInstallRoot();
}  // namespace paths

struct InstallLayout {
    wstring root;
    explicit InstallLayout(const wstring& path) : root(FullPath(path)) {}
    wstring Versions() const { return PathCombine(root, L"versions"); }
    wstring Current() const { return PathCombine(root, L"current.json"); }
    wstring Content() const { return PathCombine(root, L"content"); }
    wstring GameInputs() const { return PathCombine(root, L"game-inputs"); }
    wstring Toolchains() const { return PathCombine(root, L"toolchain"); }
    wstring Sources() const { return PathCombine(root, L"source"); }
    wstring Staging() const { return PathCombine(root, L"staging"); }
    wstring SetupCopy() const { return PathCombine(root, L"Nebula-Setup.exe"); }
    wstring VersionDir(const wstring& version) const { return PathCombine(Versions(), version); }
};

// ---- NVIDIA DLSS (installed by Setup from NVIDIA's Streamline release; Nebula ships none of it) -----------------

namespace dlss {
struct Part {
    const char* entry;
    const wchar_t* file;
    const wchar_t* sha256;
    long long size;
    long long compressed;
};
extern const Part kParts[6];
extern const char* const kLicenseEntry;
wstring Folder();                            // the runtime loads Streamline from here (GALAXY_STREAMLINE_DIR)
bool Installed();
// An NVIDIA RTX adapter has a driver (display adapter class in the registry; laptop GPUs too).
bool RtxAdapterPresent();
}  // namespace dlss

// ---- the runtime's single-instance lease -------------------------------------------------------------------------

// The runtime holds Local\Nebula.RMGE01.NativeRuntime while it runs; tests use private lease names.
bool RuntimeLeaseHeld();

// A running program from inside a folder ("Nebula.exe (pid)"), for the names given (without ".exe").
std::optional<wstring> RunningFrom(const wstring& folder, const std::vector<wstring>& names);
// Process ids of name.exe whose file is exactly path.
std::vector<DWORD> ProcessesAt(const wstring& name, const wstring& path);

}  // namespace nebula
