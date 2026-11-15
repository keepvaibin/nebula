// SPDX-License-Identifier: GPL-3.0-only
// The update's HTTP on Windows (the game's check and the Setup's download): WinHTTP, so the system's TLS, certificate
// store and proxy settings apply. HTTPS only; a redirect is followed only to HTTPS (GitHub's release files redirect to
// its download host), at most 6 times, with no cookies. The same answers as update_transport_curl.cpp: the HTTP status,
// -1 without an answer, -2 past the size limit.

#include "update_internal.h"

#include "galaxy/windows_api.h"
#include <winhttp.h>

#include <cstdio>

namespace galaxy::update::detail {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (h != nullptr) WinHttpCloseHandle(h);
    }
};

std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int units = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(units > 0 ? units : 0), L'\0');
    if (units > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), units);
    return out;
}

// Where a body goes: a string or a file, up to `limit` bytes in all (`written` counts what is there already).
struct Sink {
    std::string* text = nullptr;
    std::FILE* file = nullptr;
    std::uint64_t written = 0;
    std::uint64_t limit = 0;
    bool over = false;
    const std::atomic_bool* cancel = nullptr;
    const std::function<void(std::uint64_t)>* progress = nullptr;
};

std::FILE* open_file(const std::filesystem::path& file, const wchar_t* mode) {
    std::FILE* out = nullptr;
    return _wfopen_s(&out, file.c_str(), mode) == 0 ? out : nullptr;
}

bool canceled(const Sink& sink) { return sink.cancel != nullptr && sink.cancel->load(std::memory_order_relaxed); }

// One request with Nebula's options: GET (with `range_from` > 0, the rest of the file from there) or, with `post`, a
// POST of its bytes that follows no redirect. The HTTP status, or -1 without an answer (-2 past the sink's limit).
int perform(const std::string& url, Sink& sink, std::uint64_t range_from, int timeout_ms,
            const std::span<const std::uint8_t>* post = nullptr, std::string_view content_type = {}) {
    Handle session;
    session.h = WinHttpOpen(L"Nebula", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0u);
    if (session.h == nullptr) return -1;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // a Windows without TLS 1.3 in WinHTTP
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    // A download takes as long as it takes, unless a read stalls for 30 s; a request answers within its timeout.
    const int receive = sink.file != nullptr ? 30000 : timeout_ms;
    WinHttpSetTimeouts(session.h, timeout_ms, timeout_ms, timeout_ms, receive);
    std::wstring target = wide(url);
    for (int hop = 0; hop <= 6; ++hop) {
        wchar_t host[256] = {};
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.lpszHostName = host;
        parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(target.c_str(), 0u, 0u, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return -1;
        std::wstring object(parts.lpszUrlPath != nullptr ? parts.lpszUrlPath : L"/", parts.lpszUrlPath != nullptr ? parts.dwUrlPathLength : 1u);
        if (parts.lpszExtraInfo != nullptr) object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Handle connection;
        connection.h = WinHttpConnect(session.h, host, parts.nPort, 0u);
        if (connection.h == nullptr) return -1;
        Handle request;
        request.h = WinHttpOpenRequest(connection.h, post != nullptr ? L"POST" : L"GET", object.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request.h == nullptr) return -1;
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
        DWORD no_cookies = WINHTTP_DISABLE_COOKIES;
        WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &no_cookies, sizeof(no_cookies));
        std::wstring headers;
        if (post != nullptr) {
            headers = L"Content-Type: " + wide(content_type) + L"\r\n";
        } else {
            headers = L"Accept: application/vnd.github+json\r\n";
            if (range_from > 0u) headers += L"Range: bytes=" + std::to_wstring(range_from) + L"-\r\n";
        }
        LPVOID data = post != nullptr ? const_cast<std::uint8_t*>(post->data()) : WINHTTP_NO_REQUEST_DATA;
        const DWORD bytes = post != nullptr ? static_cast<DWORD>(post->size()) : 0u;
        if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(headers.size()), data, bytes, bytes, 0u) ||
            !WinHttpReceiveResponse(request.h, nullptr)) {
            return -1;
        }
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                 &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            return -1;
        }
        if (post == nullptr && (status == 301u || status == 302u || status == 303u || status == 307u || status == 308u)) {
            DWORD length = 0;
            WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &length,
                                WINHTTP_NO_HEADER_INDEX);
            if (length == 0u || length > 16384u) return -1;
            std::wstring location(length / sizeof(wchar_t), L'\0');
            if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location.data(), &length,
                                     WINHTTP_NO_HEADER_INDEX)) {
                return -1;
            }
            location.resize(length / sizeof(wchar_t));
            target = std::move(location);  // checked for HTTPS by the next hop's crack
            continue;
        }
        // The body: into the sink (a POST's answer is counted, never kept), stopped by a cancel or the limit.
        char buffer[64 * 1024];
        for (;;) {
            if (canceled(sink)) return -1;
            DWORD got = 0;
            if (!WinHttpReadData(request.h, buffer, sizeof(buffer), &got)) {
                return status >= 200u && status < 300u ? -1 : static_cast<int>(status);  // cut off mid-body
            }
            if (got == 0u) break;
            if (sink.written + got > sink.limit) {
                sink.over = true;
                return -2;
            }
            if (sink.text != nullptr) sink.text->append(buffer, got);
            if (sink.file != nullptr && std::fwrite(buffer, 1, got, sink.file) != got) return -1;
            sink.written += got;
            if (sink.progress != nullptr && *sink.progress) (*sink.progress)(sink.written);
        }
        return static_cast<int>(status);
    }
    return -1;  // too many redirects
}

class WinHttpTransport final : public Transport {
public:
    int get(const std::string& url, std::string& body, std::size_t max_bytes, int timeout_ms) override {
        body.clear();
        Sink sink;
        sink.text = &body;
        sink.limit = max_bytes;
        return perform(url, sink, 0u, timeout_ms);
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
        std::FILE* out = open_file(file, have > 0u ? L"ab" : L"wb");
        if (out == nullptr) return -1;
        Sink sink;
        sink.file = out;
        sink.written = have;
        sink.limit = max_bytes;
        sink.cancel = &cancel;
        sink.progress = &progress;
        int status = perform(url, sink, have, 15000);
        if (status == 200 && have > 0u) {
            // The server sent the whole file instead of the rest: start the file again.
            std::fclose(out);
            out = open_file(file, L"wb");
            if (out == nullptr) return -1;
            sink.file = out;
            sink.written = 0;
            status = perform(url, sink, 0u, 15000);
        }
        if (std::fflush(out) != 0) status = -1;
        std::fclose(out);
        return status;
    }

    // WinHTTP uses the system's proxy settings, so only the request itself can tell.
    bool unreachable(const std::string&) override { return false; }

    int post(const std::string& url, std::string_view content_type, std::span<const std::uint8_t> body, int timeout_ms,
             std::string& problem) override {
        Sink sink;
        sink.limit = 64u * 1024u;
        std::string answer;  // counted against the limit, then dropped
        sink.text = &answer;
        const int status = perform(url, sink, 0u, timeout_ms, &body, content_type);
        if (status <= 0) problem = "no answer";  // the report's own WinHTTP send names the cause (diag_report.cpp)
        return status;
    }
};

}  // namespace

Transport& platform_transport() {
    static WinHttpTransport transport;
    return transport;
}

}  // namespace galaxy::update::detail
