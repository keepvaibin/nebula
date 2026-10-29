// Zip archives (miniz), HTTPS (WinHTTP) and child processes.
#pragma once

#include "common.h"

namespace nebula {

// ---- zip ---------------------------------------------------------------------------------------------------------

class ZipReader {
public:
    struct Entry {
        std::string name;    // as stored (UTF-8)
        bool directory = false;
        unsigned long long size = 0;
        unsigned index = 0;
    };
    static std::unique_ptr<ZipReader> OpenFile(const wstring& path);
    static std::unique_ptr<ZipReader> OpenMemory(const void* data, size_t size);
    ~ZipReader();
    const std::vector<Entry>& Entries() const { return entries_; }
    const Entry* Find(const std::string& name) const;
    // The entry's bytes; InvalidData past maximumBytes or when damaged.
    Bytes Read(const Entry& entry, unsigned long long maximumBytes) const;
    void ExtractTo(const Entry& entry, const wstring& path, bool overwrite) const;

private:
    ZipReader();
    void Load(const wstring& what);
    struct State;
    std::unique_ptr<State> state_;
    std::vector<Entry> entries_;
};

class ZipWriter {
public:
    explicit ZipWriter(const wstring& path);   // created new; fails when it exists
    ~ZipWriter();
    void Add(const std::string& name, const void* data, size_t size);
    void AddFile(const std::string& name, const wstring& file);
    void Finish();

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Raw deflate, fed in pieces (a zip entry read over the network).
class Inflater {
public:
    Inflater();
    ~Inflater();
    // Inflates `size` bytes and passes the output on; InvalidData when damaged.
    void Feed(const uint8_t* data, size_t size, const std::function<void(const uint8_t*, size_t)>& output);
    bool Done() const { return done_; }

private:
    struct State;
    std::unique_ptr<State> state_;
    bool done_ = false;
};

// ---- HTTPS -------------------------------------------------------------------------------------------------------

class HttpRequest {
public:
    HttpRequest(const wstring& url, const wstring& method, const wstring& userAgent, bool followRedirects, DWORD timeoutMs);
    ~HttpRequest();
    void AddHeader(const wstring& header);
    // Sends and receives the headers. Web error on a failed connection; statuses are left to the caller.
    void Send();
    int Status() const { return status_; }
    std::optional<wstring> Header(const wstring& name) const;
    size_t Read(void* buffer, size_t size);   // 0 at the end

private:
    void* session_ = nullptr;
    void* connection_ = nullptr;
    void* request_ = nullptr;
    wstring url_;
    int status_ = 0;
};

namespace http {
extern const wchar_t* const kSetupAgent;     // "Nebula-Setup"
// Web error for a status of 400 or more, as .NET words it.
[[noreturn]] void FailStatus(const wstring& url, int status);
std::string GetString(const wstring& url, const wstring& accept);
// Downloads url to path: an existing complete file with the right hash is kept, a partial one resumes, and the
// result must match sha256 (empty: any). Retried for passing network faults.
void Fetch(const wstring& url, const wstring& path, const wstring& sha256, long long expectedSize,
           const std::function<void(long long)>& onBytes, const CancelToken& cancel);
wstring HostOf(const wstring& url);
}  // namespace http

// ---- child processes ---------------------------------------------------------------------------------------------

using Environment = std::vector<std::pair<wstring, wstring>>;
// The current environment, as name/value pairs.
Environment CurrentEnvironment();
std::vector<wchar_t> EnvironmentBlock(const Environment& environment);

// Runs programs inside a kill-on-close job object, so canceling or closing Setup stops every compiler it started.
class ProcessRunner {
public:
    ProcessRunner();
    ~ProcessRunner();
    // Runs to completion; each output line (stdout and stderr) goes to onLine. Returns the exit code; Canceled when
    // the token is canceled (the job is ended first). A null environment inherits Setup's.
    int Run(const wstring& file, const std::vector<wstring>& arguments, const wstring& workingDirectory,
            const Environment* environment, const std::function<void(const wstring&)>& onLine, const CancelToken& cancel);
    // With a preformatted command line, for programs such as msiexec that parse their own quoting.
    int RunCommandLine(const wstring& file, const wstring& commandLine, const wstring& workingDirectory,
                       const Environment* environment, const std::function<void(const wstring&)>& onLine,
                       const CancelToken& cancel);

private:
    HANDLE job_ = nullptr;
};

// Starts a program and returns at once (Process.Start without waiting).
void StartProgram(const wstring& file, const wstring& arguments, const wstring& workingDirectory);
// Opens a file, folder or link with its associated program (Explorer, the browser).
void OpenWithShell(const wstring& target, const wstring& arguments = L"");

}  // namespace nebula
