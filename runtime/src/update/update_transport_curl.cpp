// SPDX-License-Identifier: GPL-3.0-only
// The update check's HTTP on Linux: the system's libcurl (its TLS and certificate store, its proxy variables), loaded
// with dlopen so Nebula starts and plays on a system without it (the check then reports no answer). Only the handful
// of curl's stable entry points and option numbers below are used (curl.h's values, unchanged since curl 7.32).
#include "update_internal.h"

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>

namespace galaxy::update::detail {

namespace {

using CurlHandle = void;
struct CurlList;
using EasyInit = CurlHandle* (*)();
using EasySetopt = int (*)(CurlHandle*, int, ...);
using EasyPerform = int (*)(CurlHandle*);
using EasyGetinfo = int (*)(CurlHandle*, int, ...);
using EasyCleanup = void (*)(CurlHandle*);
using GlobalInit = int (*)(long);
using SlistAppend = CurlList* (*)(CurlList*, const char*);
using SlistFree = void (*)(CurlList*);

constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptHttpHeader = 10023, kOptXferInfoData = 10057;
constexpr int kOptWriteFunction = 20011, kOptXferInfoFunction = 20219;
constexpr int kOptNoProgress = 43, kOptFollowLocation = 52, kOptNoSignal = 99, kOptMaxRedirs = 68, kOptRedirProtocols = 182;
constexpr int kOptTimeoutMs = 155, kOptConnectTimeoutMs = 156, kOptLowSpeedLimit = 19, kOptLowSpeedTime = 20;
constexpr int kOptResumeFromLarge = 30116;
constexpr int kInfoResponseCode = 0x200000 + 2;
constexpr long kGlobalDefault = 3;  // CURL_GLOBAL_SSL | CURL_GLOBAL_WIN32
constexpr long kProtoHttps = 2;

struct Curl {
    EasyInit init = nullptr;
    EasySetopt setopt = nullptr;
    EasyPerform perform = nullptr;
    EasyGetinfo getinfo = nullptr;
    EasyCleanup cleanup = nullptr;
    SlistAppend slist_append = nullptr;
    SlistFree slist_free = nullptr;
    bool loaded = false;
};

const Curl& curl() {
    static Curl c;
    static std::once_flag once;
    std::call_once(once, [] {
        void* library = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) library = dlopen("libcurl-gnutls.so.4", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) return;
        c.init = reinterpret_cast<EasyInit>(dlsym(library, "curl_easy_init"));
        c.setopt = reinterpret_cast<EasySetopt>(dlsym(library, "curl_easy_setopt"));
        c.perform = reinterpret_cast<EasyPerform>(dlsym(library, "curl_easy_perform"));
        c.getinfo = reinterpret_cast<EasyGetinfo>(dlsym(library, "curl_easy_getinfo"));
        c.cleanup = reinterpret_cast<EasyCleanup>(dlsym(library, "curl_easy_cleanup"));
        c.slist_append = reinterpret_cast<SlistAppend>(dlsym(library, "curl_slist_append"));
        c.slist_free = reinterpret_cast<SlistFree>(dlsym(library, "curl_slist_free_all"));
        const auto global = reinterpret_cast<GlobalInit>(dlsym(library, "curl_global_init"));
        c.loaded = c.init && c.setopt && c.perform && c.getinfo && c.cleanup && c.slist_append && c.slist_free && global &&
                   global(kGlobalDefault) == 0;
    });
    return c;
}

struct Sink {
    std::string* text = nullptr;
    std::FILE* file = nullptr;
    std::uint64_t written = 0;
    std::uint64_t limit = 0;
    bool over = false;
    const std::atomic_bool* cancel = nullptr;
    const std::function<void(std::uint64_t)>* progress = nullptr;
};

std::size_t write_body(char* data, std::size_t size, std::size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    const std::size_t bytes = size * count;
    if (sink->cancel != nullptr && sink->cancel->load(std::memory_order_relaxed)) return 0;
    if (sink->written + bytes > sink->limit) {
        sink->over = true;
        return 0;  // curl ends the transfer with a write error
    }
    if (sink->text != nullptr) sink->text->append(data, bytes);
    if (sink->file != nullptr && std::fwrite(data, 1, bytes, sink->file) != bytes) return 0;
    sink->written += bytes;
    if (sink->progress != nullptr && *sink->progress) (*sink->progress)(sink->written);
    return bytes;
}

int on_progress(void* user, long long, long long, long long, long long) {
    const auto* sink = static_cast<const Sink*>(user);
    return sink->cancel != nullptr && sink->cancel->load(std::memory_order_relaxed) ? 1 : 0;  // non-zero aborts
}

// One GET with Nebula's options; the HTTP status, or -1 without an answer.
int perform(const std::string& url, Sink& sink, long long resume_from, int timeout_ms) {
    const Curl& c = curl();
    if (!c.loaded) return -1;
    CurlHandle* handle = c.init();
    if (handle == nullptr) return -1;
    CurlList* headers = c.slist_append(nullptr, "Accept: application/vnd.github+json");
    c.setopt(handle, kOptUrl, url.c_str());
    c.setopt(handle, kOptUserAgent, "Nebula");
    c.setopt(handle, kOptHttpHeader, headers);
    c.setopt(handle, kOptFollowLocation, 1L);
    c.setopt(handle, kOptMaxRedirs, 6L);
    c.setopt(handle, kOptRedirProtocols, kProtoHttps);  // a redirect never leaves HTTPS
    c.setopt(handle, kOptNoSignal, 1L);
    c.setopt(handle, kOptConnectTimeoutMs, static_cast<long>(timeout_ms));
    if (sink.file == nullptr) {
        c.setopt(handle, kOptTimeoutMs, static_cast<long>(timeout_ms));
    } else {
        // A download takes as long as it takes, unless it stalls (under 1 KB/s for 30 s).
        c.setopt(handle, kOptLowSpeedLimit, 1024L);
        c.setopt(handle, kOptLowSpeedTime, 30L);
    }
    c.setopt(handle, kOptWriteFunction, &write_body);
    c.setopt(handle, kOptWriteData, &sink);
    c.setopt(handle, kOptNoProgress, 0L);
    c.setopt(handle, kOptXferInfoFunction, &on_progress);
    c.setopt(handle, kOptXferInfoData, &sink);
    if (resume_from > 0) c.setopt(handle, kOptResumeFromLarge, static_cast<long long>(resume_from));
    const int result = c.perform(handle);
    long status = 0;
    c.getinfo(handle, kInfoResponseCode, &status);
    c.cleanup(handle);
    c.slist_free(headers);
    if (sink.over) return -2;
    if (result != 0 && status == 0) return -1;
    if (result != 0 && status >= 200 && status < 300) return -1;  // cut off mid-body
    return static_cast<int>(status);
}

bool proxied() {
    for (const char* name : {"https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && value[0] != '\0') return true;
    }
    return false;
}

class CurlTransport final : public Transport {
public:
    int get(const std::string& url, std::string& body, std::size_t max_bytes, int timeout_ms) override {
        body.clear();
        Sink sink;
        sink.text = &body;
        sink.limit = max_bytes;
        return perform(url, sink, 0, timeout_ms);
    }

    int download(const std::string& url, const std::filesystem::path& file, std::uint64_t max_bytes, const std::atomic_bool& cancel,
                 const std::function<void(std::uint64_t)>& progress) override {
        std::error_code error;
        std::uint64_t have = std::filesystem::exists(file, error) ? std::filesystem::file_size(file, error) : 0u;
        if (error || have > max_bytes) {
            std::filesystem::remove(file, error);
            have = 0;
        }
        if (have == max_bytes && have > 0u) return 416;  // complete already: the caller checks it
        std::FILE* out = std::fopen(file.c_str(), have > 0u ? "ab" : "wb");
        if (out == nullptr) return -1;
        Sink sink;
        sink.file = out;
        sink.written = have;
        sink.limit = max_bytes;
        sink.cancel = &cancel;
        sink.progress = &progress;
        int status = perform(url, sink, static_cast<long long>(have), 15000);
        if (status == 200 && have > 0u) {
            // The server sent the whole file instead of the rest: start the file again.
            std::fclose(out);
            out = std::fopen(file.c_str(), "wb");
            if (out == nullptr) return -1;
            sink.file = out;
            sink.written = 0;
            status = perform(url, sink, 0, 15000);
        }
        if (std::fflush(out) != 0) status = -1;
        std::fclose(out);
        return status;
    }

    bool unreachable(const std::string& url) override { return !proxied() && posix_unreachable(url, 1000); }
};

}  // namespace

Transport& platform_transport() {
    static CurlTransport transport;
    return transport;
}

}  // namespace galaxy::update::detail
