#include "common.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <thread>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace nebula {

// ---- errors ------------------------------------------------------------------------------------------------------

void Fail(ErrorKind kind, const wstring& message) { throw Error(kind, message); }

wstring Win32Message(DWORD code) {
    wchar_t* buffer = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    wstring text = length ? wstring(buffer, length) : L"Windows error " + Num(code) + L".";
    if (buffer) LocalFree(buffer);
    return Trim(text);
}

void FailWin32(DWORD code, const wstring& context) {
    ErrorKind kind = ErrorKind::Io;
    if (code == ERROR_ACCESS_DENIED) kind = ErrorKind::Access;
    else if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) kind = ErrorKind::NotFound;
    throw Error(kind, context.empty() ? Win32Message(code) : context + L": " + Win32Message(code), code);
}

wstring ErrorText(const std::exception& error) {
    if (auto* nebulaError = dynamic_cast<const Error*>(&error)) return nebulaError->message;
    if (dynamic_cast<const std::bad_alloc*>(&error)) return L"Not enough memory.";
    return Widen(error.what());
}

bool IsKind(const std::exception& error, ErrorKind kind) {
    auto* nebulaError = dynamic_cast<const Error*>(&error);
    return nebulaError && nebulaError->kind == kind;
}

bool BlockedByAntivirus(DWORD code) { return code == 225 || code == 226; }

void CancelToken::ThrowIfCanceled() const {
    if (IsCanceled()) throw Error(ErrorKind::Canceled, L"The operation was canceled.");
}

// ---- strings -----------------------------------------------------------------------------------------------------

wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    wstring text(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), text.data(), length);
    return text;
}

std::string Narrow(const wstring& text) {
    if (text.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

wstring ToLower(wstring text) {
    if (!text.empty()) CharLowerBuffW(text.data(), DWORD(text.size()));
    return text;
}

wstring ToUpper(wstring text) {
    if (!text.empty()) CharUpperBuffW(text.data(), DWORD(text.size()));
    return text;
}

bool IEquals(const wstring& a, const wstring& b) {
    return CompareStringOrdinal(a.data(), int(a.size()), b.data(), int(b.size()), TRUE) == CSTR_EQUAL;
}

bool StartsWith(const wstring& text, const wstring& prefix, bool ignoreCase) {
    if (prefix.size() > text.size()) return false;
    if (!ignoreCase) return text.compare(0, prefix.size(), prefix) == 0;
    return IEquals(text.substr(0, prefix.size()), prefix);
}

bool EndsWith(const wstring& text, const wstring& suffix, bool ignoreCase) {
    if (suffix.size() > text.size()) return false;
    wstring tail = text.substr(text.size() - suffix.size());
    return ignoreCase ? IEquals(tail, suffix) : tail == suffix;
}

size_t IFind(const wstring& text, const wstring& needle) { return ToLower(text).find(ToLower(needle)); }

wstring Trim(const wstring& text) {
    size_t start = 0, end = text.size();
    while (start < end && std::iswspace(text[start])) start++;
    while (end > start && std::iswspace(text[end - 1])) end--;
    return text.substr(start, end - start);
}

wstring TrimEnd(const wstring& text, const wstring& characters) {
    size_t end = text.size();
    while (end > 0 && characters.find(text[end - 1]) != wstring::npos) end--;
    return text.substr(0, end);
}

wstring TrimStart(const wstring& text, const wstring& characters) {
    size_t start = 0;
    while (start < text.size() && characters.find(text[start]) != wstring::npos) start++;
    return text.substr(start);
}

std::vector<wstring> Split(const wstring& text, wchar_t separator) {
    std::vector<wstring> parts;
    size_t start = 0;
    for (;;) {
        size_t at = text.find(separator, start);
        if (at == wstring::npos) { parts.push_back(text.substr(start)); return parts; }
        parts.push_back(text.substr(start, at - start));
        start = at + 1;
    }
}

wstring Replace(wstring text, const wstring& from, const wstring& to) {
    if (from.empty()) return text;
    for (size_t at = 0; (at = text.find(from, at)) != wstring::npos; at += to.size()) text.replace(at, from.size(), to);
    return text;
}

wstring Join(const std::vector<wstring>& parts, const wstring& separator) {
    wstring text;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) text += separator;
        text += parts[i];
    }
    return text;
}

bool ParseInt(const wstring& text, long long& value) {
    std::string narrow = Narrow(Trim(text));
    const char* begin = narrow.data();
    const char* end = begin + narrow.size();
    if (begin != end && *begin == '+') begin++;
    if (begin == end) return false;
    long long parsed = 0;
    auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end || parsed < INT32_MIN || parsed > INT32_MAX) return false;
    value = parsed;
    return true;
}

bool ParseDouble(const wstring& text, double& value) {
    std::string narrow = Narrow(Trim(text));
    const char* begin = narrow.data();
    const char* end = begin + narrow.size();
    if (begin != end && *begin == '+') begin++;
    if (begin == end) return false;
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

bool ParseFloat(const wstring& text, float& value) {
    double parsed;
    if (!ParseDouble(text, parsed)) return false;
    value = float(parsed);
    return true;
}

wstring Num(long long value) { return std::to_wstring(value); }

wstring FloatR(float value) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    std::string text(buffer, result.ptr);
    // C# writes exponents as E-05 / E+15.
    size_t e = text.find('e');
    if (e != std::string::npos) {
        std::string mantissa = text.substr(0, e), exponent = text.substr(e + 1);
        char sign = '+';
        if (!exponent.empty() && (exponent[0] == '-' || exponent[0] == '+')) { sign = exponent[0]; exponent = exponent.substr(1); }
        while (exponent.size() < 2) exponent = "0" + exponent;
        text = mantissa + "E" + sign + exponent;
    }
    return Widen(text);
}

wstring Fixed(double value, int decimals) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, decimals);
    return Widen(std::string(buffer, result.ptr));
}

wstring HexLower(const uint8_t* data, size_t size) {
    static const wchar_t digits[] = L"0123456789abcdef";
    wstring text(size * 2, L'0');
    for (size_t i = 0; i < size; i++) {
        text[i * 2] = digits[data[i] >> 4];
        text[i * 2 + 1] = digits[data[i] & 15];
    }
    return text;
}

static wstring OneDecimal(double value) {
    // String.Format("{0:0.0}") rounds half away from zero.
    double rounded = std::floor(value * 10.0 + 0.5) / 10.0;
    return Fixed(rounded, 1);
}

wstring FormatBytes(long long bytes) {
    if (bytes >= (1LL << 30)) return OneDecimal(double(bytes) / double(1LL << 30)) + L" GB";
    if (bytes >= (1LL << 20)) return Num((long long)std::floor(double(bytes) / double(1LL << 20) + 0.5)) + L" MB";
    return Num((long long)std::floor(double(bytes) / 1024.0 + 0.5)) + L" KB";
}

// ---- time and identifiers ----------------------------------------------------------------------------------------

wstring UtcNowIso() {
    FILETIME file;
    GetSystemTimePreciseAsFileTime(&file);
    SYSTEMTIME time;
    FileTimeToSystemTime(&file, &time);
    unsigned long long ticks = (static_cast<unsigned long long>(file.dwHighDateTime) << 32) | file.dwLowDateTime;
    wchar_t buffer[64];
    swprintf_s(buffer, L"%04d-%02d-%02dT%02d:%02d:%02d.%07lluZ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
               time.wSecond, ticks % 10000000ULL);
    return buffer;
}

wstring UtcStamp(bool milliseconds) {
    SYSTEMTIME time;
    GetSystemTime(&time);
    wchar_t buffer[64];
    if (milliseconds)
        swprintf_s(buffer, L"%04d%02d%02d-%02d%02d%02d-%03d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                   time.wSecond, time.wMilliseconds);
    else
        swprintf_s(buffer, L"%04d%02d%02d-%02d%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

wstring LocalStamp() {
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t buffer[64];
    swprintf_s(buffer, L"%04d%02d%02d-%02d%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

wstring UtcClock() {
    SYSTEMTIME time;
    GetSystemTime(&time);
    wchar_t buffer[32];
    swprintf_s(buffer, L"%02d:%02d:%02d.%03d", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return buffer;
}

wstring NewGuidN() {
    GUID guid;
    CoCreateGuid(&guid);
    wchar_t buffer[40];
    swprintf_s(buffer, L"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0],
               guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buffer;
}

// ---- environment -------------------------------------------------------------------------------------------------

std::optional<wstring> GetEnv(const wstring& name) {
    DWORD length = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (length == 0) return std::nullopt;
    wstring value(length, L'\0');
    length = GetEnvironmentVariableW(name.c_str(), value.data(), length);
    value.resize(length);
    return value;
}

wstring KnownFolder(const GUID& id) {
    wchar_t* path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path)) || !path) {
        if (path) CoTaskMemFree(path);
        Fail(ErrorKind::Io, L"A Windows folder could not be found.");
    }
    wstring result = path;
    CoTaskMemFree(path);
    return result;
}

wstring ModulePath() {
    wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    buffer.resize(length);
    return buffer;
}

wstring ModuleDirectory() { return DirName(ModulePath()); }

wstring TempDirectory() {
    wchar_t buffer[MAX_PATH + 2];
    DWORD length = GetTempPathW(MAX_PATH + 1, buffer);
    wstring path(buffer, length);
    if (!EndsWith(path, L"\\")) path += L'\\';
    return path;
}

// ---- command lines -----------------------------------------------------------------------------------------------

wstring QuoteArg(const wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == wstring::npos) return argument;
    wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { backslashes++; continue; }
        if (c == L'"') quoted.append(backslashes * 2 + 1, L'\\');
        else quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted += c;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

wstring JoinArgs(const std::vector<wstring>& arguments) {
    wstring line;
    for (const auto& argument : arguments) {
        if (!line.empty()) line += L' ';
        line += QuoteArg(argument);
    }
    return line;
}

std::vector<wstring> CommandLineArgs() {
    int count = 0;
    wchar_t** list = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<wstring> args;
    for (int i = 1; list && i < count; i++) args.push_back(list[i]);
    if (list) LocalFree(list);
    return args;
}

// ---- paths -------------------------------------------------------------------------------------------------------

bool IsRooted(const wstring& path) {
    return (!path.empty() && (path[0] == L'\\' || path[0] == L'/')) || (path.size() >= 2 && path[1] == L':');
}

wstring PathCombine(const wstring& a, const wstring& b) {
    if (b.empty()) return a;
    if (a.empty() || IsRooted(b)) return b;
    wchar_t last = a.back();
    if (last == L'\\' || last == L'/' || last == L':') return a + b;
    return a + L"\\" + b;
}

wstring DirName(const wstring& path) {
    size_t at = path.find_last_of(L"\\/");
    if (at == wstring::npos) return L"";
    if (at == 2 && path.size() >= 3 && path[1] == L':') return path.substr(0, 3) == path ? L"" : path.substr(0, 3);
    if (at == 0) return path.size() > 1 ? path.substr(0, 1) : L"";
    return path.substr(0, at);
}

wstring FileName(const wstring& path) {
    size_t at = path.find_last_of(L"\\/");
    return at == wstring::npos ? path : path.substr(at + 1);
}

wstring FullPath(const wstring& path) {
    if (path.empty()) Fail(ErrorKind::Argument, L"The path is empty.");
    DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (length == 0) FailWin32(GetLastError(), path);
    wstring full(length, L'\0');
    length = GetFullPathNameW(path.c_str(), length, full.data(), nullptr);
    full.resize(length);
    return full;
}

wstring Slashes(wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

// \\?\ form for every Win32 call that may pass MAX_PATH (named streams lengthen save paths).
static wstring NativePath(const wstring& path) {
    if (StartsWith(path, L"\\\\?\\")) return path;
    wstring full = FullPath(path);
    return StartsWith(full, L"\\\\") ? L"\\\\?\\UNC\\" + full.substr(2) : L"\\\\?\\" + full;
}

static DWORD Attributes(const wstring& path) { return GetFileAttributesW(NativePath(path).c_str()); }

bool FileExists(const wstring& path) {
    if (path.empty()) return false;
    DWORD a = Attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const wstring& path) {
    if (path.empty()) return false;
    DWORD a = Attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

long long FileSize(const wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(NativePath(path).c_str(), GetFileExInfoStandard, &data)) FailWin32(GetLastError(), path);
    return (long long(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

bool IsReparsePoint(const wstring& path) {
    DWORD a = Attributes(path);
    if (a == INVALID_FILE_ATTRIBUTES) FailWin32(GetLastError(), path);
    return (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

wstring FinalPath(const wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), path);
    wstring buffer(1024, L'\0');
    DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), 0);
    DWORD error = GetLastError();
    CloseHandle(handle);
    if (length == 0 || length >= buffer.size()) FailWin32(error, path);
    buffer.resize(length);
    if (StartsWith(buffer, L"\\\\?\\UNC\\", true)) return L"\\\\" + buffer.substr(8);
    if (StartsWith(buffer, L"\\\\?\\")) return buffer.substr(4);
    return buffer;
}

void CreateDirs(const wstring& path) {
    if (path.empty() || DirExists(path)) return;
    wstring full = FullPath(path);
    wstring parent = DirName(full);
    if (!parent.empty() && parent != full && !DirExists(parent)) CreateDirs(parent);
    if (!CreateDirectoryW(NativePath(full).c_str(), nullptr)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS || !DirExists(full)) FailWin32(error, L"Could not create " + full);
    }
}

void DeleteFileOrThrow(const wstring& path) {
    wstring native = NativePath(path);
    DWORD a = GetFileAttributesW(native.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(native.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (!DeleteFileW(native.c_str())) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) FailWin32(error, L"Could not delete " + path);
    }
}

void DeleteFileQuietly(const wstring& path) {
    try { DeleteFileOrThrow(path); } catch (const Error&) {}
}

static void DeleteTreeOnce(const wstring& path) {
    wstring native = TrimEnd(NativePath(path), L"\\");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((native + L"\\*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            wstring child = native + L"\\" + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(child.c_str(), data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    if (!RemoveDirectoryW(child.c_str())) FailWin32(GetLastError(), L"Could not delete " + child.substr(4));
                } else {
                    DeleteTreeOnce(child);
                }
            } else if (!DeleteFileW(child.c_str())) {
                DWORD error = GetLastError();
                FindClose(find);
                FailWin32(error, L"Could not delete " + child.substr(4));
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    DWORD a = GetFileAttributesW(native.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(native.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (!RemoveDirectoryW(native.c_str())) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) FailWin32(error, L"Could not delete " + path);
    }
}

void DeleteTree(const wstring& path) {
    if (!DirExists(path)) return;
    for (int attempt = 0;; attempt++) {
        try {
            DeleteTreeOnce(path);
            return;
        } catch (const Error& error) {
            if (attempt >= 5 || (error.kind != ErrorKind::Io && error.kind != ErrorKind::Access)) throw;
            Sleep(500);
        }
    }
}

void RemoveEmptyDirectory(const wstring& path) {
    if (!RemoveDirectoryW(NativePath(path).c_str())) FailWin32(GetLastError(), L"Could not delete " + path);
}

static void List(const wstring& directory, bool files, bool dirs, bool recursive, std::vector<wstring>& out) {
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((TrimEnd(NativePath(directory), L"\\") + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES) return;
        FailWin32(error, directory);
    }
    std::vector<wstring> subdirectories;
    do {
        wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        wstring child = PathCombine(directory, name);
        bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir) {
            if (dirs) out.push_back(child);
            if (recursive && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) subdirectories.push_back(child);
        } else if (files) {
            out.push_back(child);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    for (const auto& sub : subdirectories) List(sub, files, dirs, true, out);
}

std::vector<wstring> ListFiles(const wstring& directory, bool recursive) {
    std::vector<wstring> out;
    List(directory, true, false, recursive, out);
    return out;
}

std::vector<wstring> ListDirs(const wstring& directory, bool recursive) {
    std::vector<wstring> out;
    List(directory, false, true, recursive, out);
    return out;
}

std::vector<wstring> ListEntries(const wstring& directory) {
    std::vector<wstring> out;
    List(directory, true, true, false, out);
    return out;
}

bool IsDirectoryEmpty(const wstring& directory) { return ListEntries(directory).empty(); }

// ---- file contents -----------------------------------------------------------------------------------------------

namespace {
struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit FileHandle(HANDLE handle) : h(handle) {}
    ~FileHandle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
};

[[noreturn]] void FailRead(DWORD error, const wstring& path) {
    if (BlockedByAntivirus(error))
        throw Error(ErrorKind::Io,
                    L"Windows Security blocked " + path + L", one of Nebula's own files, so it could not be read. "
                    L"Nebula is built from its public source code at https://github.com/" + wstring(build::kRepository) +
                    L"; please report the file to Microsoft as a false detection.", error);
    FailWin32(error, path);
}
}  // namespace

Bytes ReadAllBytes(const wstring& path) {
    FileHandle file(CreateFileW(NativePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.h == INVALID_HANDLE_VALUE) FailRead(GetLastError(), path);
    LARGE_INTEGER size;
    GetFileSizeEx(file.h, &size);
    Bytes bytes(size_t(size.QuadPart));
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD chunk = DWORD(std::min<size_t>(bytes.size() - done, 1u << 24)), read = 0;
        if (!ReadFile(file.h, bytes.data() + done, chunk, &read, nullptr)) FailRead(GetLastError(), path);
        if (read == 0) break;
        done += read;
    }
    bytes.resize(done);
    return bytes;
}

wstring ReadAllText(const wstring& path) {
    Bytes bytes = ReadAllBytes(path);
    size_t skip = bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
    return Widen(std::string(bytes.begin() + skip, bytes.end()));
}

std::vector<wstring> ReadAllLines(const wstring& path) {
    wstring text = ReadAllText(path);
    std::vector<wstring> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\r' || text[i] == L'\n') {
            lines.push_back(text.substr(start, i - start));
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') i++;
            start = i + 1;
        }
    }
    if (start < text.size()) lines.push_back(text.substr(start));
    return lines;
}

void WriteAllBytes(const wstring& path, const void* data, size_t size, bool createNew, bool flush) {
    FileHandle file(CreateFileW(NativePath(path).c_str(), GENERIC_WRITE, 0, nullptr, createNew ? CREATE_NEW : CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), path);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        DWORD chunk = DWORD(std::min<size_t>(size - done, 1u << 24)), written = 0;
        if (!WriteFile(file.h, bytes + done, chunk, &written, nullptr)) FailWin32(GetLastError(), path);
        done += written;
    }
    if (flush) FlushFileBuffers(file.h);
}

void WriteAllText(const wstring& path, const wstring& text) {
    std::string utf8 = Narrow(text);
    WriteAllBytes(path, utf8.data(), utf8.size());
}

void ReplaceFileWith(const wstring& temporary, const wstring& path) {
    if (FileExists(path)) {
        if (!ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            DWORD error = GetLastError();
            DeleteFileQuietly(temporary);
            FailWin32(error, L"Could not replace " + path);
        }
    } else {
        MoveFileOrThrow(temporary, path);
    }
}

void WriteAllTextAtomic(const wstring& path, const wstring& text) {
    CreateDirs(DirName(FullPath(path)));
    wstring temp = path + L".tmp-" + NewGuidN();
    WriteAllText(temp, text);
    ReplaceFileWith(temp, path);
}

void CopyFileOrThrow(const wstring& from, const wstring& to, bool overwrite) {
    if (!CopyFileW(NativePath(from).c_str(), NativePath(to).c_str(), overwrite ? FALSE : TRUE)) {
        DWORD error = GetLastError();
        if (BlockedByAntivirus(error)) FailRead(error, from);
        FailWin32(error, L"Could not copy " + from + L" to " + to);
    }
}

void MoveFileOrThrow(const wstring& from, const wstring& to) {
    if (!MoveFileExW(NativePath(from).c_str(), NativePath(to).c_str(), MOVEFILE_COPY_ALLOWED))
        FailWin32(GetLastError(), L"Could not move " + from + L" to " + to);
}

// ---- moving and copying trees ------------------------------------------------------------------------------------

const wchar_t* const kHeldAdvice = L" Close Nebula and any window or program using these files, then try again.";

static std::optional<Error> RetryMove(const wstring& source, const wstring& destination) {
    if (DirExists(destination) || FileExists(destination))
        Fail(ErrorKind::Io, L"Cannot move " + source + L" to " + destination + L", which already exists.");
    std::optional<Error> refused;
    for (int attempt = 0; attempt < 20; attempt++) {
        if (attempt != 0) Sleep(500);
        if (MoveFileExW(NativePath(source).c_str(), NativePath(destination).c_str(), 0)) return std::nullopt;
        DWORD error = GetLastError();
        refused = Error(error == ERROR_ACCESS_DENIED ? ErrorKind::Access : ErrorKind::Io,
                        L"Could not move " + source + L" to " + destination + L": " + Win32Message(error), error);
    }
    return refused;
}

void MoveDirectory(const wstring& source, const wstring& destination, const std::function<void(const wstring&)>& log) {
    auto refused = RetryMove(source, destination);
    if (!refused) return;
    log(L"Could not move " + source + L" (" + refused->message + L"); copying it instead.");
    try {
        CopyTree(source, destination);
    } catch (const Error& error) {
        try { DeleteTree(destination); } catch (const Error&) {}
        throw Error(ErrorKind::Io, refused->message + kHeldAdvice + L" (" + error.message + L")");
    }
    try { DeleteTree(source); } catch (const Error&) {}
}

void MoveDirectoryInPlace(const wstring& source, const wstring& destination) {
    auto refused = RetryMove(source, destination);
    if (refused) throw Error(ErrorKind::Io, refused->message + kHeldAdvice, refused->win32);
}

void CopyTree(const wstring& source, const wstring& destination) {
    wstring from = TrimEnd(FullPath(source), L"\\");
    wstring to = FullPath(destination);
    CreateDirs(to);
    for (const auto& dir : ListDirs(from, true)) CreateDirs(PathCombine(to, dir.substr(from.size() + 1)));
    for (const auto& file : ListFiles(from, true)) CopyFileOrThrow(file, PathCombine(to, file.substr(from.size() + 1)), true);
}

namespace {
struct StreamFind {
    HANDLE h = INVALID_HANDLE_VALUE;
    WIN32_FIND_STREAM_DATA data{};
    DWORD error = 0;
    explicit StreamFind(const wstring& path) {
        h = FindFirstStreamW(NativePath(path).c_str(), FindStreamInfoStandard, &data, 0);
        if (h == INVALID_HANDLE_VALUE) error = GetLastError();
    }
    ~StreamFind() { if (h != INVALID_HANDLE_VALUE) FindClose(h); }
    bool Next() { return FindNextStreamW(h, &data) != FALSE; }
};

Bytes ReadHandle(HANDLE handle, const wstring& what) {
    Bytes bytes;
    uint8_t buffer[65536];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(handle, buffer, sizeof(buffer), &read, nullptr)) FailWin32(GetLastError(), what);
        if (read == 0) return bytes;
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
}
}  // namespace

void CopyNamedStreams(const wstring& source, const wstring& target) {
    StreamFind find(source);
    if (find.h == INVALID_HANDLE_VALUE) {
        if (find.error == ERROR_HANDLE_EOF) return;
        FailWin32(find.error, source);
    }
    do {
        wstring name = find.data.cStreamName;
        if (name == L"::$DATA") continue;
        FileHandle input(CreateFileW((NativePath(source) + name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (input.h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), source + name);
        FileHandle output(CreateFileW((NativePath(target) + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (output.h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), target + name);
        Bytes bytes = ReadHandle(input.h, source + name);
        DWORD written = 0;
        if (!bytes.empty() && !WriteFile(output.h, bytes.data(), DWORD(bytes.size()), &written, nullptr))
            FailWin32(GetLastError(), target + name);
    } while (find.Next());
}

void CopyTreeWithStreams(wstring source, const wstring& target, const std::function<void(const wstring&)>& onFile) {
    if (IsReparsePoint(source) && !IEquals(source, FinalPath(source))) source = FinalPath(source);
    CreateDirs(target);
    CopyNamedStreams(source, target);
    for (const auto& directory : ListDirs(source, false)) {
        if (IsReparsePoint(directory)) Fail(ErrorKind::Io, L"Refusing to copy a linked folder: " + directory);
        CopyTreeWithStreams(directory, PathCombine(target, FileName(directory)), onFile);
    }
    for (const auto& file : ListFiles(source, false)) {
        if (IsReparsePoint(file)) Fail(ErrorKind::Io, L"Refusing to copy a linked file: " + file);
        wstring destination = PathCombine(target, FileName(file));
        if (!CopyFileW(NativePath(file).c_str(), NativePath(destination).c_str(), TRUE))
            FailWin32(GetLastError(), L"Copy failed: " + file);
        if (onFile) onFile(file);
    }
}

std::map<wstring, wstring> StreamDigests(const wstring& path) {
    std::map<wstring, wstring> result;
    StreamFind find(path);
    if (find.h == INVALID_HANDLE_VALUE) {
        if (find.error == ERROR_HANDLE_EOF) return result;
        FailWin32(find.error, path);
    }
    do {
        wstring name = find.data.cStreamName;
        FileHandle handle(CreateFileW((name == L"::$DATA" ? NativePath(path) : NativePath(path) + name).c_str(), GENERIC_READ,
                                      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (handle.h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), path + name);
        Bytes bytes = ReadHandle(handle.h, path + name);
        result[name] = Sha256Bytes(bytes.data(), bytes.size());
    } while (find.Next());
    return result;
}

std::vector<std::pair<wstring, Bytes>> ReadNamedStreams(const wstring& path, long long maximumBytes) {
    std::vector<std::pair<wstring, Bytes>> streams;
    StreamFind find(path);
    if (find.h == INVALID_HANDLE_VALUE) {
        DWORD error = find.error;
        if (error == ERROR_HANDLE_EOF || error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED ||
            error == ERROR_INVALID_PARAMETER)
            return streams;
        FailWin32(error, path);
    }
    do {
        wstring name = find.data.cStreamName;
        if (name == L"::$DATA") continue;
        if (!StartsWith(name, L":") || !EndsWith(name, L":$DATA"))
            Fail(ErrorKind::Io, L"Unexpected stream " + name + L" on " + path + L".");
        if (find.data.StreamSize.QuadPart > maximumBytes)
            Fail(ErrorKind::Io, L"The stream " + name + L" of " + path + L" is too large.");
        FileHandle handle(CreateFileW((NativePath(path) + name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (handle.h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), path + name);
        Bytes bytes = ReadHandle(handle.h, path + name);
        if ((long long)bytes.size() != find.data.StreamSize.QuadPart)
            Fail(ErrorKind::Io, L"The stream " + name + L" of " + path + L" changed while it was read.");
        streams.emplace_back(name.substr(1, name.size() - 7), std::move(bytes));
    } while (find.Next());
    DWORD last = GetLastError();
    if (last != ERROR_HANDLE_EOF) FailWin32(last, path);
    return streams;
}

void WriteNamedStream(const wstring& path, const wstring& name, const Bytes& bytes) {
    FileHandle handle(CreateFileW((NativePath(path) + L":" + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (handle.h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), path + L":" + name);
    DWORD written = 0;
    if (!bytes.empty() && !WriteFile(handle.h, bytes.data(), DWORD(bytes.size()), &written, nullptr))
        FailWin32(GetLastError(), path + L":" + name);
    FlushFileBuffers(handle.h);
}

// ---- hashes ------------------------------------------------------------------------------------------------------

Sha256::Sha256() {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        Fail(ErrorKind::InvalidOperation, L"SHA-256 is not available.");
    algorithm_ = algorithm;
    hash_ = hash;
}

Sha256::~Sha256() {
    if (hash_) BCryptDestroyHash(hash_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
}

void Sha256::Update(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    while (size) {
        ULONG chunk = ULONG(std::min<size_t>(size, 1u << 30));
        BCryptHashData(hash_, const_cast<PUCHAR>(bytes), chunk, 0);
        bytes += chunk;
        size -= chunk;
    }
}

std::vector<uint8_t> Sha256::Finish() {
    std::vector<uint8_t> digest(32);
    BCryptFinishHash(hash_, digest.data(), 32, 0);
    return digest;
}

wstring Sha256::FinishHex() {
    auto digest = Finish();
    return HexLower(digest.data(), digest.size());
}

wstring Sha256File(const wstring& path) {
    FileHandle file(CreateFileW(NativePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.h == INVALID_HANDLE_VALUE) FailRead(GetLastError(), path);
    Sha256 sha;
    std::vector<uint8_t> buffer(1 << 20);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file.h, buffer.data(), DWORD(buffer.size()), &read, nullptr)) FailRead(GetLastError(), path);
        if (read == 0) break;
        sha.Update(buffer.data(), read);
    }
    return sha.FinishHex();
}

wstring Sha256Bytes(const void* data, size_t size) {
    Sha256 sha;
    sha.Update(data, size);
    return sha.FinishHex();
}

wstring Sha256Text(const wstring& text) {
    std::string utf8 = Narrow(text);
    return Sha256Bytes(utf8.data(), utf8.size());
}

Bytes Base64Decode(const std::string& text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    Bytes out;
    int buffer = 0, bits = 0, padding = 0, count = 0;
    for (char c : text) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') { padding++; count++; continue; }
        int v = value(c);
        if (v < 0 || padding) Fail(ErrorKind::InvalidData, L"Invalid Base64 text.");
        buffer = (buffer << 6) | v;
        bits += 6;
        count++;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((buffer >> bits) & 0xFF));
        }
    }
    if (count % 4 != 0 || padding > 2) Fail(ErrorKind::InvalidData, L"Invalid Base64 text.");
    return out;
}

std::string Base64Encode(const uint8_t* data, size_t size) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < size; i += 3) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < size) v |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < size) v |= data[i + 2];
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += i + 1 < size ? table[(v >> 6) & 63] : '=';
        out += i + 2 < size ? table[v & 63] : '=';
    }
    return out;
}

static std::string XmlElement(const std::string& xml, const std::string& name) {
    size_t start = xml.find("<" + name + ">");
    size_t end = xml.find("</" + name + ">");
    if (start == std::string::npos || end == std::string::npos || end < start) return {};
    start += name.size() + 2;
    return xml.substr(start, end - start);
}

bool VerifyRsaSha256File(const wstring& path, const Bytes& signature, const wstring& publicKeyXml) {
    std::string xml = Narrow(publicKeyXml);
    Bytes modulus = Base64Decode(XmlElement(xml, "Modulus"));
    Bytes exponent = Base64Decode(XmlElement(xml, "Exponent"));
    if (modulus.empty() || exponent.empty()) return false;
    std::vector<uint8_t> blob(sizeof(BCRYPT_RSAKEY_BLOB) + exponent.size() + modulus.size());
    auto* header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
    header->Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header->BitLength = ULONG(modulus.size() * 8);
    header->cbPublicExp = ULONG(exponent.size());
    header->cbModulus = ULONG(modulus.size());
    header->cbPrime1 = header->cbPrime2 = 0;
    std::copy(exponent.begin(), exponent.end(), blob.begin() + sizeof(BCRYPT_RSAKEY_BLOB));
    std::copy(modulus.begin(), modulus.end(), blob.begin() + sizeof(BCRYPT_RSAKEY_BLOB) + exponent.size());

    std::vector<uint8_t> digest;
    {
        Bytes bytes = ReadAllBytes(path);
        Sha256 sha;
        sha.Update(bytes.data(), bytes.size());
        digest = sha.Finish();
    }
    BCRYPT_ALG_HANDLE rsa = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool valid = false;
    if (BCryptOpenAlgorithmProvider(&rsa, BCRYPT_RSA_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptImportKeyPair(rsa, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key, blob.data(), ULONG(blob.size()), 0) >= 0) {
            BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
            valid = BCryptVerifySignature(key, &padding, digest.data(), ULONG(digest.size()), const_cast<PUCHAR>(signature.data()),
                                          ULONG(signature.size()), BCRYPT_PAD_PKCS1) >= 0;
            BCryptDestroyKey(key);
        }
        BCryptCloseAlgorithmProvider(rsa, 0);
    }
    return valid;
}

// ---- JSON --------------------------------------------------------------------------------------------------------

const Json* Json::Find(const wstring& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& member : members)
        if (member.first == key) return &member.second;
    return nullptr;
}

Json& Json::Set(const wstring& key, Json value) {
    type = Type::Object;
    for (auto& member : members)
        if (member.first == key) { member.second = std::move(value); return member.second; }
    members.emplace_back(key, std::move(value));
    return members.back().second;
}

namespace {
struct JsonParser {
    const wstring& s;
    size_t i = 0;
    int depth = 0;
    explicit JsonParser(const wstring& text) : s(text) {}

    [[noreturn]] void Bad() { Fail(ErrorKind::InvalidData, L"Invalid JSON."); }
    void Space() { while (i < s.size() && (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n')) i++; }
    bool Take(wchar_t c) { Space(); if (i < s.size() && s[i] == c) { i++; return true; } return false; }
    void Expect(wchar_t c) { if (!Take(c)) Bad(); }
    bool Word(const wchar_t* word) {
        size_t n = wcslen(word);
        if (s.compare(i, n, word) == 0) { i += n; return true; }
        return false;
    }

    wstring String() {
        Expect(L'"');
        wstring out;
        while (i < s.size()) {
            wchar_t c = s[i++];
            if (c == L'"') return out;
            if (c != L'\\') { out += c; continue; }
            if (i >= s.size()) Bad();
            wchar_t e = s[i++];
            switch (e) {
                case L'"': out += L'"'; break;
                case L'\\': out += L'\\'; break;
                case L'/': out += L'/'; break;
                case L'b': out += L'\b'; break;
                case L'f': out += L'\f'; break;
                case L'n': out += L'\n'; break;
                case L'r': out += L'\r'; break;
                case L't': out += L'\t'; break;
                case L'u': {
                    if (i + 4 > s.size()) Bad();
                    unsigned v = 0;
                    for (int k = 0; k < 4; k++) {
                        wchar_t h = s[i++];
                        v <<= 4;
                        if (h >= L'0' && h <= L'9') v |= h - L'0';
                        else if (h >= L'a' && h <= L'f') v |= h - L'a' + 10;
                        else if (h >= L'A' && h <= L'F') v |= h - L'A' + 10;
                        else Bad();
                    }
                    out += wchar_t(v);
                    break;
                }
                default: Bad();
            }
        }
        Bad();
    }

    Json Value() {
        if (++depth > 256) Bad();
        Space();
        if (i >= s.size()) Bad();
        Json result;
        wchar_t c = s[i];
        if (c == L'{') {
            i++;
            result = Json::Object();
            if (!Take(L'}')) {
                do {
                    Space();
                    wstring key = String();
                    Expect(L':');
                    result.Set(key, Value());
                } while (Take(L','));
                Expect(L'}');
            }
        } else if (c == L'[') {
            i++;
            result = Json::Array();
            if (!Take(L']')) {
                do result.items.push_back(Value());
                while (Take(L','));
                Expect(L']');
            }
        } else if (c == L'"') {
            result = Json::Str(String());
        } else if (Word(L"true")) {
            result = Json::Bool(true);
        } else if (Word(L"false")) {
            result = Json::Bool(false);
        } else if (Word(L"null")) {
            result = Json::Null();
        } else {
            size_t start = i;
            if (s[i] == L'-') i++;
            bool fraction = false;
            while (i < s.size() && (iswdigit(s[i]) || s[i] == L'.' || s[i] == L'e' || s[i] == L'E' || s[i] == L'+' || s[i] == L'-')) {
                if (s[i] == L'.' || s[i] == L'e' || s[i] == L'E') fraction = true;
                i++;
            }
            std::string number = Narrow(s.substr(start, i - start));
            if (number.empty() || number == "-") Bad();
            long long whole = 0;
            if (!fraction) {
                auto r = std::from_chars(number.data(), number.data() + number.size(), whole);
                if (r.ec == std::errc() && r.ptr == number.data() + number.size()) { result = Json::Int(whole); depth--; return result; }
            }
            double value = 0;
            auto r = std::from_chars(number.data(), number.data() + number.size(), value);
            if (r.ec != std::errc() || r.ptr != number.data() + number.size()) Bad();
            result = Json::Double(value);
        }
        depth--;
        return result;
    }
};

void Escape(const wstring& text, wstring& out) {
    out += L'"';
    for (wchar_t c : text) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buffer[8];
                    swprintf_s(buffer, L"\\u%04x", unsigned(c));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += L'"';
}

void Write(const Json& value, wstring& out, int indent) {
    auto newline = [&](int level) { out += L'\n'; out.append(size_t(level) * 2, L' '); };
    switch (value.type) {
        case Json::Type::Null: out += L"null"; break;
        case Json::Type::Bool: out += value.boolean ? L"true" : L"false"; break;
        case Json::Type::Number:
            if (value.integer) out += Num(value.whole);
            else {
                char buffer[64];
                auto r = std::to_chars(buffer, buffer + sizeof(buffer), value.number);
                out += Widen(std::string(buffer, r.ptr));
            }
            break;
        case Json::Type::String: Escape(value.text, out); break;
        case Json::Type::Array:
            if (value.items.empty()) { out += L"[]"; break; }
            out += L'[';
            for (size_t k = 0; k < value.items.size(); k++) {
                newline(indent + 1);
                Write(value.items[k], out, indent + 1);
                if (k + 1 < value.items.size()) out += L',';
            }
            newline(indent);
            out += L']';
            break;
        case Json::Type::Object:
            if (value.members.empty()) { out += L"{}"; break; }
            out += L'{';
            for (size_t k = 0; k < value.members.size(); k++) {
                newline(indent + 1);
                Escape(value.members[k].first, out);
                out += L": ";
                Write(value.members[k].second, out, indent + 1);
                if (k + 1 < value.members.size()) out += L',';
            }
            newline(indent);
            out += L'}';
            break;
    }
}
}  // namespace

Json ParseJson(const wstring& text) {
    JsonParser parser(text);
    Json value = parser.Value();
    parser.Space();
    if (parser.i != text.size()) parser.Bad();
    return value;
}

Json LoadJson(const wstring& path) {
    Json value = ParseJson(ReadAllText(path));
    if (!value.IsObject()) Fail(ErrorKind::InvalidData, L"Expected a JSON object.");
    return value;
}

wstring SerializeJson(const Json& value) {
    wstring out;
    Write(value, out, 0);
    return out;
}

void SaveJson(const wstring& path, const Json& value) { WriteAllTextAtomic(path, SerializeJson(value) + L"\n"); }

wstring JScalarText(const Json& value) {
    switch (value.type) {
        case Json::Type::String: return value.text;
        case Json::Type::Bool: return value.boolean ? L"True" : L"False";
        case Json::Type::Number: {
            if (value.integer) return Num(value.whole);
            char buffer[64];
            auto r = std::to_chars(buffer, buffer + sizeof(buffer), value.number);
            return Widen(std::string(buffer, r.ptr));
        }
        default: return L"";
    }
}

std::optional<wstring> JStr(const Json* object, const wstring& key) {
    const Json* value = object ? object->Find(key) : nullptr;
    if (!value || value->IsNull() || value->IsArray() || value->IsObject()) return std::nullopt;
    return JScalarText(*value);
}

long long JLong(const Json* object, const wstring& key) {
    const Json* value = object ? object->Find(key) : nullptr;
    if (!value || value->IsNull()) return 0;
    if (value->type == Json::Type::Number) return value->integer ? value->whole : (long long)std::llround(value->number);
    if (value->type == Json::Type::Bool) return value->boolean ? 1 : 0;
    if (value->type == Json::Type::String) {
        long long parsed = 0;
        double d = 0;
        if (ParseInt(value->text, parsed)) return parsed;
        if (ParseDouble(value->text, d)) return (long long)std::llround(d);
    }
    Fail(ErrorKind::InvalidData, L"Expected a number for " + key + L".");
}

bool JBool(const Json* object, const wstring& key) {
    const Json* value = object ? object->Find(key) : nullptr;
    return value && value->type == Json::Type::Bool && value->boolean;
}

const Json* JObj(const Json* object, const wstring& key) {
    const Json* value = object ? object->Find(key) : nullptr;
    return value && value->IsObject() ? value : nullptr;
}

const Json* JList(const Json* object, const wstring& key) {
    const Json* value = object ? object->Find(key) : nullptr;
    return value && value->IsArray() ? value : nullptr;
}

// ---- locations ---------------------------------------------------------------------------------------------------

namespace paths {
wstring LocalAppData() { return KnownFolder(FOLDERID_LocalAppData); }
wstring DefaultInstallRoot() { return PathCombine(LocalAppData(), L"Programs", L"Nebula"); }
wstring DataRoot() {
    auto overridden = GetEnv(L"NEBULA_DATA_ROOT");
    return overridden && !overridden->empty() ? *overridden : PathCombine(LocalAppData(), L"Nebula");
}
wstring Saves() { return PathCombine(DataRoot(), L"saves"); }
wstring SettingsFile() { return PathCombine(DataRoot(), L"settings.json"); }
wstring Sessions() { return PathCombine(DataRoot(), L"sessions"); }
wstring SetupLogs() { return PathCombine(DataRoot(), L"setup-logs"); }
wstring ShaderCache() { return PathCombine(DataRoot(), L"shadercache"); }

std::optional<wstring> RegisteredInstallRoot() {
    wchar_t buffer[4096];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Nebula", L"InstallRoot", RRF_RT_REG_SZ, &type, buffer, &size) != ERROR_SUCCESS)
        return std::nullopt;
    wstring value = buffer;
    if (value.empty()) return std::nullopt;
    return value;
}
}  // namespace paths

// ---- DLSS --------------------------------------------------------------------------------------------------------

namespace dlss {
const char* const kLicenseEntry = "bin/x64/nvngx_dlss.license.txt";
const Part kParts[6] = {
    {"bin/x64/sl.interposer.dll", L"sl.interposer.dll", L"8c87c9499461da561edd529aa9bf7831d67d7b94ebb1c1a5ed54ef4934e1ea4c", 652928, 284550},
    {"bin/x64/sl.common.dll", L"sl.common.dll", L"82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d", 843392, 390298},
    {"bin/x64/sl.dlss.dll", L"sl.dlss.dll", L"73bf52c0cfaa5900a8f3f4a91306e4625e7cca696dfb305aae44c9f97b582e1f", 422016, 194964},
    {"bin/x64/nvngx_dlss.dll", L"nvngx_dlss.dll", L"3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983", 58956912, 31123319},
    {"bin/x64/nvngx_dlss.license.txt", L"nvngx_dlss.license.txt", L"3027f23ca5a46dd9cb8183fbd522983a86f64d7daac5982912bf9f214671f294", 26727, 9256},
    {"license.txt", L"streamline.license.txt", L"7b6f23e7d6f3ad6292f9308d2b42cdc3d82ae4e9b2abb55f230279d83bedd43d", 1591, 883},
};

wstring Folder() { return PathCombine(paths::DataRoot(), L"vendor", L"streamline-2.14.1"); }

bool Installed() {
    for (const auto& part : kParts) {
        wstring path = PathCombine(Folder(), part.file);
        if (!FileExists(path) || FileSize(path) != part.size) return false;
    }
    return true;
}

bool RtxAdapterPresent() {
    HKEY adapters = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}", 0,
                      KEY_READ, &adapters) != ERROR_SUCCESS)
        return false;
    bool found = false;
    wchar_t name[256];
    for (DWORD index = 0; !found; index++) {
        DWORD length = 256;
        LONG result = RegEnumKeyExW(adapters, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS || length != 4) continue;
        wchar_t id[512], description[512];
        DWORD idSize = sizeof(id), descriptionSize = sizeof(description);
        if (RegGetValueW(adapters, name, L"MatchingDeviceId", RRF_RT_REG_SZ, nullptr, id, &idSize) != ERROR_SUCCESS) continue;
        if (RegGetValueW(adapters, name, L"DriverDesc", RRF_RT_REG_SZ, nullptr, description, &descriptionSize) != ERROR_SUCCESS) continue;
        found = IFind(id, L"ven_10de") != wstring::npos && IFind(description, L"RTX") != wstring::npos;
    }
    RegCloseKey(adapters);
    return found;
}
}  // namespace dlss

// ---- running programs --------------------------------------------------------------------------------------------

bool RuntimeLeaseHeld() {
    HANDLE lease = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\Nebula.RMGE01.NativeRuntime");
    if (!lease) return false;
    DWORD wait = WaitForSingleObject(lease, 0);
    bool acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    if (acquired) ReleaseMutex(lease);
    CloseHandle(lease);
    return !acquired;
}

static std::optional<wstring> ProcessFile(DWORD id) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process) return std::nullopt;
    wchar_t buffer[32768];
    DWORD size = 32768;
    std::optional<wstring> path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) path = wstring(buffer, size);
    CloseHandle(process);
    return path;
}

template <typename Visit>
static void EachProcess(Visit visit) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        if (!visit(entry)) break;
    CloseHandle(snapshot);
}

std::optional<wstring> RunningFrom(const wstring& folder, const std::vector<wstring>& names) {
    wstring prefix = TrimEnd(FullPath(folder), L"\\") + L"\\";
    for (const auto& name : names) {
        std::optional<wstring> found;
        EachProcess([&](const PROCESSENTRY32W& entry) {
            if (!IEquals(entry.szExeFile, name + L".exe")) return true;
            auto path = ProcessFile(entry.th32ProcessID);
            if (path && StartsWith(*path, prefix, true)) {
                found = name + L".exe (" + Num(entry.th32ProcessID) + L")";
                return false;
            }
            return true;
        });
        if (found) return found;
    }
    return std::nullopt;
}

std::vector<DWORD> ProcessesAt(const wstring& name, const wstring& path) {
    std::vector<DWORD> ids;
    EachProcess([&](const PROCESSENTRY32W& entry) {
        if (IEquals(entry.szExeFile, name + L".exe")) {
            auto file = ProcessFile(entry.th32ProcessID);
            if (file && IEquals(*file, path)) ids.push_back(entry.th32ProcessID);
        }
        return true;
    });
    return ids;
}

}  // namespace nebula
