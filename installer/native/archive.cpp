#include "archive.h"

#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <thread>

#include "third_party/miniz/miniz.h"

#pragma comment(lib, "winhttp.lib")

namespace nebula {

// ---- zip ---------------------------------------------------------------------------------------------------------

struct ZipReader::State {
    mz_zip_archive zip{};
    FILE* file = nullptr;
    bool open = false;
};

ZipReader::ZipReader() : state_(std::make_unique<State>()) {}

ZipReader::~ZipReader() {
    if (state_->open) mz_zip_reader_end(&state_->zip);
    if (state_->file) fclose(state_->file);
}

void ZipReader::Load(const wstring& what) {
    unsigned count = mz_zip_reader_get_num_files(&state_->zip);
    for (unsigned i = 0; i < count; i++) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&state_->zip, i, &stat)) Fail(ErrorKind::InvalidData, what + L" is damaged.");
        Entry entry;
        entry.name = stat.m_filename;
        entry.directory = mz_zip_reader_is_file_a_directory(&state_->zip, i) != 0;
        entry.size = stat.m_uncomp_size;
        entry.index = i;
        entries_.push_back(std::move(entry));
    }
}

std::unique_ptr<ZipReader> ZipReader::OpenFile(const wstring& path) {
    std::unique_ptr<ZipReader> reader(new ZipReader());
    if (_wfopen_s(&reader->state_->file, path.c_str(), L"rb") != 0 || !reader->state_->file) {
        DWORD error = GetLastError();
        FailWin32(error ? error : ERROR_FILE_NOT_FOUND, path);
    }
    if (!mz_zip_reader_init_cfile(&reader->state_->zip, reader->state_->file, 0, 0))
        Fail(ErrorKind::InvalidData, L"This is not a zip archive, or the archive is damaged: " + FileName(path));
    reader->state_->open = true;
    reader->Load(FileName(path));
    return reader;
}

std::unique_ptr<ZipReader> ZipReader::OpenMemory(const void* data, size_t size) {
    std::unique_ptr<ZipReader> reader(new ZipReader());
    if (!mz_zip_reader_init_mem(&reader->state_->zip, data, size, 0))
        Fail(ErrorKind::InvalidData, L"The embedded archive is damaged.");
    reader->state_->open = true;
    reader->Load(L"The embedded archive");
    return reader;
}

const ZipReader::Entry* ZipReader::Find(const std::string& name) const {
    for (const auto& entry : entries_)
        if (entry.name == name) return &entry;
    return nullptr;
}

Bytes ZipReader::Read(const Entry& entry, unsigned long long maximumBytes) const {
    if (entry.size > maximumBytes) Fail(ErrorKind::InvalidData, Widen(entry.name) + L" is too large.");
    Bytes bytes(size_t(entry.size));
    if (entry.size && !mz_zip_reader_extract_to_mem(&state_->zip, entry.index, bytes.data(), bytes.size(), 0))
        Fail(ErrorKind::InvalidData, L"The archive is damaged.");
    return bytes;
}

namespace {
struct FileSink {
    HANDLE h;
    bool failed = false;
    DWORD error = 0;
};

size_t WriteSink(void* opaque, mz_uint64, const void* buffer, size_t size) {
    auto* sink = static_cast<FileSink*>(opaque);
    const uint8_t* bytes = static_cast<const uint8_t*>(buffer);
    size_t done = 0;
    while (done < size) {
        DWORD written = 0;
        if (!WriteFile(sink->h, bytes + done, DWORD(std::min<size_t>(size - done, 1u << 24)), &written, nullptr)) {
            sink->failed = true;
            sink->error = GetLastError();
            return 0;
        }
        done += written;
    }
    return size;
}
}  // namespace

void ZipReader::ExtractTo(const Entry& entry, const wstring& path, bool overwrite) const {
    CreateDirs(DirName(path));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), path);
    FileSink sink{h};
    bool ok = mz_zip_reader_extract_to_callback(&state_->zip, entry.index, WriteSink, &sink, 0) != 0;
    CloseHandle(h);
    if (sink.failed) FailWin32(sink.error, path);
    if (!ok) Fail(ErrorKind::InvalidData, L"The archive entry " + Widen(entry.name) + L" is damaged.");
}

struct ZipWriter::State {
    mz_zip_archive zip{};
    FILE* file = nullptr;
    bool open = false;
    wstring path;
};

ZipWriter::ZipWriter(const wstring& path) : state_(std::make_unique<State>()) {
    state_->path = path;
    if (FileExists(path)) Fail(ErrorKind::Io, L"The file " + path + L" already exists.");
    if (_wfopen_s(&state_->file, path.c_str(), L"wb") != 0 || !state_->file) FailWin32(GetLastError(), path);
    if (!mz_zip_writer_init_cfile(&state_->zip, state_->file, 0)) Fail(ErrorKind::Io, L"Could not write " + path + L".");
    state_->open = true;
}

ZipWriter::~ZipWriter() {
    if (state_->open) mz_zip_writer_end(&state_->zip);
    if (state_->file) fclose(state_->file);
}

void ZipWriter::Add(const std::string& name, const void* data, size_t size) {
    if (!mz_zip_writer_add_mem(&state_->zip, name.c_str(), data, size, MZ_DEFAULT_LEVEL))
        Fail(ErrorKind::Io, L"Could not write " + state_->path + L".");
}

void ZipWriter::AddFile(const std::string& name, const wstring& file) {
    FILE* source = nullptr;
    if (_wfopen_s(&source, file.c_str(), L"rb") != 0 || !source) FailWin32(GetLastError(), file);
    _fseeki64(source, 0, SEEK_END);
    long long size = _ftelli64(source);
    _fseeki64(source, 0, SEEK_SET);
    bool ok = mz_zip_writer_add_cfile(&state_->zip, name.c_str(), source, mz_uint64(size), nullptr, nullptr, 0, MZ_DEFAULT_LEVEL,
                                      nullptr, 0, nullptr, 0) != 0;
    fclose(source);
    if (!ok) Fail(ErrorKind::Io, L"Could not write " + state_->path + L".");
}

void ZipWriter::Finish() {
    bool ok = mz_zip_writer_finalize_archive(&state_->zip) != 0;
    mz_zip_writer_end(&state_->zip);
    state_->open = false;
    bool flushed = fflush(state_->file) == 0;
    fclose(state_->file);
    state_->file = nullptr;
    if (!ok || !flushed) Fail(ErrorKind::Io, L"Could not write " + state_->path + L".");
}

struct Inflater::State {
    mz_stream stream{};
};

Inflater::Inflater() : state_(std::make_unique<State>()) {
    if (mz_inflateInit2(&state_->stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) Fail(ErrorKind::InvalidData, L"Inflate failed.");
}

Inflater::~Inflater() { mz_inflateEnd(&state_->stream); }

void Inflater::Feed(const uint8_t* data, size_t size, const std::function<void(const uint8_t*, size_t)>& output) {
    uint8_t buffer[1 << 16];
    state_->stream.next_in = data;
    state_->stream.avail_in = unsigned(size);
    while (!done_ && (state_->stream.avail_in > 0)) {
        state_->stream.next_out = buffer;
        state_->stream.avail_out = sizeof(buffer);
        int status = mz_inflate(&state_->stream, MZ_NO_FLUSH);
        size_t produced = sizeof(buffer) - state_->stream.avail_out;
        if (produced) output(buffer, produced);
        if (status == MZ_STREAM_END) { done_ = true; break; }
        if (status != MZ_OK && status != MZ_BUF_ERROR) Fail(ErrorKind::InvalidData, L"The compressed data is damaged.");
        if (status == MZ_BUF_ERROR && produced == 0) break;
    }
    // Drain output still held after the input is used up.
    while (!done_) {
        state_->stream.next_out = buffer;
        state_->stream.avail_out = sizeof(buffer);
        int status = mz_inflate(&state_->stream, MZ_NO_FLUSH);
        size_t produced = sizeof(buffer) - state_->stream.avail_out;
        if (produced) output(buffer, produced);
        if (status == MZ_STREAM_END) { done_ = true; break; }
        if (produced == 0) break;
        if (status != MZ_OK && status != MZ_BUF_ERROR) Fail(ErrorKind::InvalidData, L"The compressed data is damaged.");
    }
}

// ---- HTTPS -------------------------------------------------------------------------------------------------------

namespace {
[[noreturn]] void FailWeb(const wstring& url, DWORD error) {
    (void)url;
    wstring text;
    HMODULE module = GetModuleHandleW(L"winhttp.dll");
    wchar_t* buffer = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  module, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    text = length ? Trim(wstring(buffer, length)) : L"The connection failed (error " + Num(error) + L").";
    if (buffer) LocalFree(buffer);
    throw Error(ErrorKind::Web, text, error);
}

const wchar_t* ReasonPhrase(int status) {
    switch (status) {
        case 400: return L"Bad Request";
        case 401: return L"Unauthorized";
        case 403: return L"Forbidden";
        case 404: return L"Not Found";
        case 410: return L"Gone";
        case 416: return L"Requested Range Not Satisfiable";
        case 429: return L"Too Many Requests";
        case 500: return L"Internal Server Error";
        case 502: return L"Bad Gateway";
        case 503: return L"Server Unavailable";
        case 504: return L"Gateway Timeout";
        default: return L"Error";
    }
}
}  // namespace

HttpRequest::HttpRequest(const wstring& url, const wstring& method, const wstring& userAgent, bool followRedirects, DWORD timeoutMs)
    : url_(url) {
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[512], path[8192];
    parts.lpszHostName = host;
    parts.dwHostNameLength = 512;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 8192;
    wchar_t extra[8192];
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 8192;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) Fail(ErrorKind::Argument, L"Invalid address: " + url);
    session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_)
        session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) FailWeb(url, GetLastError());
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /* TLS 1.3 */;
    if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    WinHttpSetTimeouts(session_, int(timeoutMs), int(timeoutMs), int(timeoutMs), int(timeoutMs));
    connection_ = WinHttpConnect(session_, wstring(host, parts.dwHostNameLength).c_str(), parts.nPort, 0);
    if (!connection_) FailWeb(url, GetLastError());
    wstring object = wstring(path, parts.dwUrlPathLength) + wstring(extra, parts.dwExtraInfoLength);
    request_ = WinHttpOpenRequest(connection_, method.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request_) FailWeb(url, GetLastError());
    DWORD policy = followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request_, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
}

HttpRequest::~HttpRequest() {
    if (request_) WinHttpCloseHandle(request_);
    if (connection_) WinHttpCloseHandle(connection_);
    if (session_) WinHttpCloseHandle(session_);
}

void HttpRequest::AddHeader(const wstring& header) {
    WinHttpAddRequestHeaders(request_, header.c_str(), DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
}

void HttpRequest::Send() {
    if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request_, nullptr))
        FailWeb(url_, GetLastError());
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    status_ = int(status);
}

std::optional<wstring> HttpRequest::Header(const wstring& name) const {
    DWORD size = 0;
    WinHttpQueryHeaders(request_, WINHTTP_QUERY_CUSTOM, name.c_str(), WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return std::nullopt;
    wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request_, WINHTTP_QUERY_CUSTOM, name.c_str(), value.data(), &size, WINHTTP_NO_HEADER_INDEX))
        return std::nullopt;
    value.resize(size / sizeof(wchar_t));
    return value;
}

size_t HttpRequest::Read(void* buffer, size_t size) {
    DWORD read = 0;
    if (!WinHttpReadData(request_, buffer, DWORD(std::min<size_t>(size, 1u << 20)), &read)) FailWeb(url_, GetLastError());
    return read;
}

namespace http {
const wchar_t* const kSetupAgent = L"Nebula-Setup";

void FailStatus(const wstring&, int status) {
    throw Error(ErrorKind::Web, L"The remote server returned an error: (" + Num(status) + L") " + ReasonPhrase(status) + L".", 0, status);
}

wstring HostOf(const wstring& url) {
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[512];
    parts.lpszHostName = host;
    parts.dwHostNameLength = 512;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return url;
    return wstring(host, parts.dwHostNameLength);
}

static wstring DisplayName(const wstring& url) {
    wstring path = url;
    size_t query = path.find_first_of(L"?#");
    if (query != wstring::npos) path = path.substr(0, query);
    path = TrimEnd(path, L"/");
    wstring name = path.substr(path.find_last_of(L'/') + 1);
    // Uri.UnescapeDataString
    std::string bytes;
    std::string narrow = Narrow(name);
    for (size_t i = 0; i < narrow.size(); i++) {
        if (narrow[i] == '%' && i + 2 < narrow.size() && isxdigit((unsigned char)narrow[i + 1]) && isxdigit((unsigned char)narrow[i + 2])) {
            bytes += char(std::stoi(narrow.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            bytes += narrow[i];
        }
    }
    return Widen(bytes);
}

std::string GetString(const wstring& url, const wstring& accept) {
    HttpRequest request(url, L"GET", kSetupAgent, true, 30000);
    if (!accept.empty()) request.AddHeader(L"Accept: " + accept);
    request.Send();
    if (request.Status() >= 400) FailStatus(url, request.Status());
    std::string body;
    char buffer[65536];
    for (size_t read; (read = request.Read(buffer, sizeof(buffer))) > 0;) body.append(buffer, read);
    return body;
}

static void Transfer(const wstring& url, const wstring& partial, long long expectedSize, const std::function<void(long long)>& onBytes,
                     const CancelToken& cancel) {
    long long existing = FileExists(partial) ? FileSize(partial) : 0;
    if (expectedSize > 0 && existing > expectedSize) {
        DeleteFileOrThrow(partial);
        existing = 0;
    }
    HttpRequest request(url, L"GET", kSetupAgent, true, 60000);
    if (existing > 0) request.AddHeader(L"Range: bytes=" + Num(existing) + L"-");
    request.Send();
    if (request.Status() >= 400) FailStatus(url, request.Status());
    bool resumed = existing > 0 && request.Status() == 206;
    HANDLE output = CreateFileW(partial.c_str(), FILE_APPEND_DATA | GENERIC_WRITE, 0, nullptr, resumed ? OPEN_ALWAYS : CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), partial);
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{output};
    if (resumed) {
        LARGE_INTEGER zero{};
        SetFilePointerEx(output, zero, nullptr, FILE_END);
    }
    long long total = resumed ? existing : 0;
    if (onBytes) onBytes(total);
    std::vector<uint8_t> buffer(1 << 20);
    for (size_t read; (read = request.Read(buffer.data(), buffer.size())) > 0;) {
        if (cancel.IsCanceled()) throw Error(ErrorKind::Canceled, L"The operation was canceled.");
        DWORD written = 0;
        if (!WriteFile(output, buffer.data(), DWORD(read), &written, nullptr)) FailWin32(GetLastError(), partial);
        total += long long(read);
        if (onBytes) onBytes(total);
    }
}

void Fetch(const wstring& url, const wstring& path, const wstring& sha256, long long expectedSize,
           const std::function<void(long long)>& onBytes, const CancelToken& cancel) {
    if (FileExists(path) && (sha256.empty() || Sha256File(path) == sha256)) {
        if (onBytes) onBytes(FileSize(path));
        return;
    }
    CreateDirs(DirName(path));
    wstring partial = path + L".partial";
    for (int attempt = 1;; attempt++) {
        try {
            Transfer(url, partial, expectedSize, onBytes, cancel);
            if (!sha256.empty()) {
                wstring actual = Sha256File(partial);
                if (actual != sha256) {
                    DeleteFileOrThrow(partial);
                    Fail(ErrorKind::InvalidData, FileName(path) + L" failed its integrity check (expected SHA-256 " + sha256 +
                                                     L", received " + actual + L").");
                }
            }
            if (FileExists(path)) DeleteFileOrThrow(path);
            MoveFileOrThrow(partial, path);
            return;
        } catch (const Error& error) {
            if (error.kind == ErrorKind::Canceled) throw;
            if (error.kind == ErrorKind::InvalidData) {
                if (attempt >= 2) throw;
            } else if (error.kind == ErrorKind::Web) {
                if (cancel.IsCanceled()) throw;
                bool missing = error.http == 404 || error.http == 410;
                if (missing || attempt >= 4)
                    throw Error(ErrorKind::Web, L"Could not download " + DisplayName(url) + L" from " + HostOf(url) + L": " + error.message,
                                error.win32, error.http);
                Sleep(DWORD(2000 * attempt));
            } else {
                if (attempt >= 4 || cancel.IsCanceled()) throw;
                Sleep(DWORD(2000 * attempt));
            }
        }
    }
}
}  // namespace http

// ---- child processes ---------------------------------------------------------------------------------------------

Environment CurrentEnvironment() {
    Environment environment;
    wchar_t* block = GetEnvironmentStringsW();
    for (const wchar_t* p = block; p && *p; p += wcslen(p) + 1) {
        wstring entry = p;
        size_t equals = entry.find(L'=', 1);
        if (equals == wstring::npos) continue;
        environment.emplace_back(entry.substr(0, equals), entry.substr(equals + 1));
    }
    if (block) FreeEnvironmentStringsW(block);
    return environment;
}

std::vector<wchar_t> EnvironmentBlock(const Environment& environment) {
    Environment sorted = environment;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return CompareStringOrdinal(a.first.data(), int(a.first.size()), b.first.data(), int(b.first.size()), TRUE) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> block;
    for (const auto& pair : sorted) {
        wstring entry = pair.first + L"=" + pair.second;
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    if (block.size() == 1) block.push_back(L'\0');
    return block;
}

ProcessRunner::ProcessRunner() {
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (!job_) FailWin32(GetLastError(), L"Could not create a job object");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof(info)))
        FailWin32(GetLastError(), L"Could not configure a job object");
}

ProcessRunner::~ProcessRunner() {
    if (job_) CloseHandle(job_);
}

int ProcessRunner::Run(const wstring& file, const std::vector<wstring>& arguments, const wstring& workingDirectory,
                       const Environment* environment, const std::function<void(const wstring&)>& onLine, const CancelToken& cancel) {
    return RunCommandLine(file, JoinArgs(arguments), workingDirectory, environment, onLine, cancel);
}

int ProcessRunner::RunCommandLine(const wstring& file, const wstring& commandLine, const wstring& workingDirectory,
                                  const Environment* environment, const std::function<void(const wstring&)>& onLine,
                                  const CancelToken& cancel) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &inherit, 1 << 16)) FailWin32(GetLastError(), L"Could not create a pipe");
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nul;
    startup.StartupInfo.hStdOutput = writePipe;
    startup.StartupInfo.hStdError = writePipe;
    // Only the pipe and NUL are inherited.
    HANDLE inherited[2] = {writePipe, nul};
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<uint8_t> attributes(attributeSize);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize);
    UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                              nul != INVALID_HANDLE_VALUE ? sizeof(inherited) : sizeof(HANDLE), nullptr, nullptr);

    wstring line = L"\"" + file + L"\"" + (commandLine.empty() ? L"" : L" " + commandLine);
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    std::vector<wchar_t> block;
    if (environment) block = EnvironmentBlock(*environment);
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(file.c_str(), mutableLine.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                                  environment ? block.data() : nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                  &startup.StartupInfo, &process);
    DWORD startError = GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(writePipe);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) {
        CloseHandle(readPipe);
        FailWin32(startError, L"Could not start " + FileName(file));
    }
    AssignProcessToJobObject(job_, process.hProcess);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    std::thread reader([&] {
        std::string pending;
        char buffer[8192];
        auto emit = [&](std::string text) {
            if (onLine) onLine(Widen(text));
        };
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
            pending.append(buffer, read);
            size_t start = 0;
            for (size_t i = 0; i < pending.size(); i++) {
                if (pending[i] == '\n' || pending[i] == '\r') {
                    if (pending[i] == '\r' && i + 1 == pending.size()) break;   // wait for a following '\n'
                    emit(pending.substr(start, i - start));
                    if (pending[i] == '\r' && pending[i + 1] == '\n') i++;
                    start = i + 1;
                }
            }
            pending.erase(0, start);
        }
        if (!pending.empty()) {
            if (pending.back() == '\r') pending.pop_back();
            emit(pending);
        }
    });

    bool canceled = false;
    while (WaitForSingleObject(process.hProcess, 200) == WAIT_TIMEOUT) {
        if (cancel.IsCanceled()) {
            TerminateJobObject(job_, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
            canceled = true;
            break;
        }
    }
    if (canceled) TerminateJobObject(job_, 1);
    reader.join();
    CloseHandle(readPipe);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (canceled) throw Error(ErrorKind::Canceled, L"The operation was canceled.");
    return int(code);
}

void StartProgram(const wstring& file, const wstring& arguments, const wstring& workingDirectory) {
    wstring line = L"\"" + file + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(file.c_str(), mutableLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process))
        FailWin32(GetLastError(), L"Could not start " + FileName(file));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void OpenWithShell(const wstring& target, const wstring& arguments) {
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = target.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) FailWin32(GetLastError(), L"Could not open " + target);
}

}  // namespace nebula
