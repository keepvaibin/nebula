#include "galaxy/discord_presence.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#endif

namespace galaxy::discord {

namespace {
// Discord's IPC opcodes.
constexpr std::uint32_t kOpHandshake = 0u;
constexpr std::uint32_t kOpFrame = 1u;
constexpr std::uint32_t kOpClose = 2u;

std::mutex s_mutex;
std::condition_variable s_wake;
std::string s_client_id;
Activity s_activity;
bool (*s_enabled_provider)() = nullptr;
bool s_dirty = true;
bool s_started = false;
}  // namespace

std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20u) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            } else {
                out += ch;  // UTF-8 passes through
            }
        }
    }
    return out + "\"";
}

std::string frame(std::uint32_t opcode, std::string_view json) {
    std::string out(8u, '\0');
    const std::uint32_t length = static_cast<std::uint32_t>(json.size());
    for (unsigned i = 0; i < 4u; ++i) {
        out[i] = static_cast<char>((opcode >> (8u * i)) & 0xFFu);
        out[4u + i] = static_cast<char>((length >> (8u * i)) & 0xFFu);
    }
    out.append(json);
    return out;
}

std::string handshake_json(std::string_view client_id) {
    return "{\"v\":1,\"client_id\":" + json_string(client_id) + "}";
}

std::string set_activity_json(std::uint32_t pid, const Activity& activity, std::int64_t start_seconds,
                              std::uint32_t nonce, bool clear) {
    std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":";
    if (clear) {
        json += "null";
    } else {
        json += "{";
        bool first = true;
        const auto field = [&](const char* name, const std::string& value) {
            if (value.empty()) return;
            json += first ? "" : ",";
            json += "\"" + std::string(name) + "\":" + json_string(value);
            first = false;
        };
        field("details", activity.details);
        field("state", activity.state);
        json += first ? "" : ",";
        json += "\"timestamps\":{\"start\":" + std::to_string(start_seconds) + "}";
        json += ",\"assets\":{\"large_image\":\"nebula\",\"large_text\":\"Super Mario Galaxy (Nebula)\"}}";
    }
    json += "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
    return json;
}

#if defined(_WIN32)
namespace {
// The pipe is opened for overlapped I/O: a write or a read waits on an event with a time limit, not on a sleep.
class Pipe {
public:
    Pipe() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Pipe() {
        close();
        if (event_ != nullptr) CloseHandle(event_);
    }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    bool open() {
        if (event_ == nullptr) return false;
        for (int i = 0; i < 10; ++i) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            handle_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                                  nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) return true;
        }
        handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    bool is_open() const { return handle_ != INVALID_HANDLE_VALUE; }
    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    bool write(const std::string& bytes) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event_;
        DWORD written = 0;
        if (WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &overlapped) == FALSE) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(event_, 2000) != WAIT_OBJECT_0) {
                (void)CancelIoEx(handle_, &overlapped);
                (void)GetOverlappedResult(handle_, &overlapped, &written, TRUE);
                return false;
            }
        }
        return GetOverlappedResult(handle_, &overlapped, &written, FALSE) != FALSE && written == bytes.size();
    }
    // Waits up to `milliseconds` for Discord to send something and appends it to `out`; false when the pipe broke.
    bool read_wait(std::string* out, DWORD milliseconds) {
        char buffer[4096];
        OVERLAPPED overlapped{};
        overlapped.hEvent = event_;
        DWORD got = 0;
        if (ReadFile(handle_, buffer, sizeof(buffer), nullptr, &overlapped) == FALSE) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            if (WaitForSingleObject(event_, milliseconds) != WAIT_OBJECT_0) {
                // Nothing came in time: what arrived while the read was being cancelled still counts.
                (void)CancelIoEx(handle_, &overlapped);
                (void)GetOverlappedResult(handle_, &overlapped, &got, TRUE);
                if (got != 0 && out != nullptr) out->append(buffer, got);
                return true;
            }
        }
        if (GetOverlappedResult(handle_, &overlapped, &got, FALSE) == FALSE) return false;
        if (out != nullptr) out->append(buffer, got);
        return true;
    }
    // Reads whatever Discord has sent (its replies), without waiting; false when the pipe broke.
    bool drain(std::string* first_frame) {
        for (;;) {
            DWORD available = 0;
            if (PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr) == FALSE) return false;
            if (available == 0) return true;
            if (!read_wait(first_frame, 1000)) return false;
        }
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE event_ = nullptr;
};

void worker() {
    Pipe pipe;
    std::uint32_t nonce = 1u;
    const std::int64_t start = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count();
    auto next_attempt = std::chrono::steady_clock::now();
    bool shown = false;
    for (;;) {
        std::string client_id;
        Activity activity;
        bool enabled = false, dirty = false;
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_wake.wait_for(lock, std::chrono::seconds(1), [] { return s_dirty; });
            client_id = s_client_id;
            activity = s_activity;
            enabled = s_enabled_provider == nullptr || s_enabled_provider();
            dirty = s_dirty;
            s_dirty = false;
        }
        if (!enabled || client_id.empty()) {
            if (pipe.is_open()) {
                // Leaving: Discord is told on a best effort, the pipe closes either way.
                if (shown) (void)pipe.write(frame(kOpFrame, set_activity_json(GetCurrentProcessId(), {}, start, nonce++, true)));
                (void)pipe.write(frame(kOpClose, "{}"));
                pipe.close();
                std::fprintf(stderr, "[discord] status off\n");
            }
            shown = false;
            continue;
        }
        if (!pipe.is_open()) {
            if (std::chrono::steady_clock::now() < next_attempt) continue;
            next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            if (!pipe.open()) continue;
            std::string ready;
            bool ok = pipe.write(frame(kOpHandshake, handshake_json(client_id)));
            // Discord answers READY (or closes on an unknown application) within a moment.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (ok && ready.size() < 8u && std::chrono::steady_clock::now() < deadline) {
                ok = pipe.read_wait(&ready, 300);
            }
            if (!ok || ready.size() < 8u || static_cast<unsigned char>(ready[0]) != kOpFrame) {
                std::fprintf(stderr, "[discord] no answer to the handshake; retrying in 15 s\n");
                pipe.close();
                continue;
            }
            std::fprintf(stderr, "[discord] connected\n");
            dirty = true;
        }
        if (dirty) {
            if (!pipe.write(frame(kOpFrame, set_activity_json(GetCurrentProcessId(), activity, start, nonce++, false)))) {
                pipe.close();
                shown = false;
                continue;
            }
            shown = true;
            std::fprintf(stderr, "[discord] status: %s | %s\n", activity.details.c_str(), activity.state.c_str());
        }
        if (!pipe.drain(nullptr)) {
            pipe.close();
            shown = false;
            std::lock_guard<std::mutex> lock(s_mutex);
            s_dirty = true;  // shown again once Discord is back
        }
    }
}
}  // namespace
#endif

void start(std::string client_id) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_started) return;
    s_started = true;
    s_client_id = std::move(client_id);
#if defined(_WIN32)
    if (s_client_id.empty()) return;
    std::thread thread(worker);
    (void)SetThreadPriority(thread.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
    thread.detach();  // ends with the process
#endif
}

void set_enabled_provider(bool (*provider)()) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_enabled_provider = provider;
}

void set_activity(const Activity& activity) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_activity.details == activity.details && s_activity.state == activity.state) return;
    s_activity = activity;
    s_dirty = true;
    s_wake.notify_one();
}

}  // namespace galaxy::discord
