#include "galaxy/output_relay.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace galaxy::output {
namespace {

// A diagnostics recording writes about 40 KB/s, so the cap holds over ten minutes of a stalled consumer.
constexpr std::size_t kMemoryCap = 32u * 1024u * 1024u;
constexpr DWORD kPipeBytes = 1u << 20u;
constexpr DWORD kReadBytes = 64u * 1024u;

class Relay {
public:
    bool start(DWORD std_id, int fd, std::FILE* stream, const wchar_t* name) noexcept;
    void drain(std::chrono::steady_clock::time_point deadline) noexcept;
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    void read_loop() noexcept;
    void write_loop() noexcept;

    // The read end is overlapped: a synchronous read pending on it would hold drain()'s PeekNamedPipe until
    // more bytes arrive.
    HANDLE read_ = nullptr;
    HANDLE read_event_ = nullptr;
    OVERLAPPED read_overlapped_{};
    std::atomic<bool> read_waiting_{false};
    HANDLE destination_ = nullptr;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::vector<char>> pending_;
    std::size_t pending_bytes_ = 0u;
    std::uint64_t accepted_ = 0u;
    std::uint64_t forwarded_ = 0u;
    std::atomic<std::uint64_t> dropped_{0u};
};

bool Relay::start(DWORD std_id, int fd, std::FILE* stream, const wchar_t* name) noexcept {
    const HANDLE original = GetStdHandle(std_id);
    if (original == nullptr || original == INVALID_HANDLE_VALUE) {
        return false;
    }
    const DWORD type = GetFileType(original);
    if (type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK) {
        return false;
    }
    const HANDLE process = GetCurrentProcess();
    if (!DuplicateHandle(process, original, process, &destination_, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return false;
    }
    const std::wstring pipe = L"\\\\.\\pipe\\nebula-output-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(std_id);
    read_ = CreateNamedPipeW(pipe.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0,
                             kPipeBytes, 0, nullptr);
    const HANDLE write = read_ != INVALID_HANDLE_VALUE
                             ? CreateFileW(pipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)
                             : INVALID_HANDLE_VALUE;
    read_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (write == INVALID_HANDLE_VALUE || read_event_ == nullptr) {
        if (read_ != INVALID_HANDLE_VALUE) CloseHandle(read_);
        if (read_event_ != nullptr) CloseHandle(read_event_);
        CloseHandle(destination_);
        return false;
    }
    std::fflush(stream);
    // The stream keeps its text or binary mode, so the bytes reaching the destination do not change.
    const int mode = _setmode(fd, _O_BINARY);
    if (mode != -1) {
        (void)_setmode(fd, mode);
    }
    const int relay_fd = _open_osfhandle(reinterpret_cast<intptr_t>(write), mode == -1 ? _O_TEXT : mode);
    if (relay_fd == -1 || _dup2(relay_fd, fd) != 0) {
        if (relay_fd != -1) {
            _close(relay_fd);
        } else {
            CloseHandle(write);
        }
        CloseHandle(read_);
        CloseHandle(read_event_);
        CloseHandle(destination_);
        return false;
    }
    _close(relay_fd);
    SetStdHandle(std_id, reinterpret_cast<HANDLE>(_get_osfhandle(fd)));

    std::thread([this, name] {
        SetThreadDescription(GetCurrentThread(), name);
        read_loop();
    }).detach();
    std::thread([this] {
        SetThreadDescription(GetCurrentThread(), L"Nebula Output Writer");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        write_loop();
    }).detach();
    return true;
}

void Relay::read_loop() noexcept {
    std::vector<char> buffer(kReadBytes);
    for (;;) {
        read_overlapped_ = OVERLAPPED{};
        read_overlapped_.hEvent = read_event_;
        DWORD got = 0;
        if (!ReadFile(read_, buffer.data(), kReadBytes, nullptr, &read_overlapped_) &&
            GetLastError() != ERROR_IO_PENDING) {
            return;
        }
        read_waiting_.store(true, std::memory_order_release);
        const bool read = GetOverlappedResult(read_, &read_overlapped_, &got, TRUE) != FALSE;
        read_waiting_.store(false, std::memory_order_release);
        if (!read || got == 0u) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        accepted_ += got;
        if (pending_bytes_ + got > kMemoryCap) {
            dropped_.fetch_add(got, std::memory_order_relaxed);
            forwarded_ += got;
            continue;
        }
        pending_.emplace_back(buffer.begin(), buffer.begin() + got);
        pending_bytes_ += got;
        ready_.notify_all();
    }
}

void Relay::write_loop() noexcept {
    bool destination_open = true;
    for (;;) {
        std::vector<char> chunk;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return !pending_.empty(); });
            chunk = std::move(pending_.front());
            pending_.pop_front();
        }
        std::size_t offset = 0u;
        while (destination_open && offset < chunk.size()) {
            DWORD written = 0;
            if (!WriteFile(destination_, chunk.data() + offset, static_cast<DWORD>(chunk.size() - offset), &written,
                           nullptr)) {
                // The consumer is gone (a closed launcher): the rest is discarded so writers never block.
                destination_open = false;
                break;
            }
            offset += written;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        pending_bytes_ -= chunk.size();
        forwarded_ += chunk.size();
        ready_.notify_all();
    }
}

void Relay::drain(std::chrono::steady_clock::time_point deadline) noexcept {
    for (;;) {
        // Everything written before the call is in the pipe, in a completed read not yet counted, or counted.
        const bool reading_nothing = read_waiting_.load(std::memory_order_acquire) &&
                                     !HasOverlappedIoCompleted(&read_overlapped_);
        DWORD queued = 0;
        const bool pipe_empty = PeekNamedPipe(read_, nullptr, 0, nullptr, &queued, nullptr) && queued == 0u;
        std::unique_lock<std::mutex> lock(mutex_);
        if (reading_nothing && pipe_empty && forwarded_ == accepted_) {
            return;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return;
        }
        ready_.wait_for(lock, std::chrono::milliseconds(2));
    }
}

// Process-lifetime objects: the relay threads use them until the process ends.
Relay* s_output = nullptr;
Relay* s_error = nullptr;

void drain_at_exit() noexcept {
    drain_relay(std::chrono::milliseconds(2000));
}

}  // namespace

void start_relay() noexcept {
    if (s_output != nullptr || s_error != nullptr) {
        return;
    }
    auto* output = new (std::nothrow) Relay();
    auto* error = new (std::nothrow) Relay();
    if (output != nullptr && output->start(STD_OUTPUT_HANDLE, 1, stdout, L"Nebula Output Relay")) {
        s_output = output;
    } else {
        delete output;
    }
    if (error != nullptr && error->start(STD_ERROR_HANDLE, 2, stderr, L"Nebula Error Relay")) {
        s_error = error;
    } else {
        delete error;
    }
    if (s_output != nullptr || s_error != nullptr) {
        (void)std::atexit(drain_at_exit);
    }
}

void drain_relay(std::chrono::milliseconds limit) noexcept {
    std::fflush(stdout);
    std::fflush(stderr);
    const auto deadline = std::chrono::steady_clock::now() + limit;
    if (s_output != nullptr) {
        s_output->drain(deadline);
    }
    if (s_error != nullptr) {
        s_error->drain(deadline);
    }
}

std::uint64_t relay_dropped_bytes() noexcept {
    return (s_output != nullptr ? s_output->dropped() : 0u) + (s_error != nullptr ? s_error->dropped() : 0u);
}

}  // namespace galaxy::output
