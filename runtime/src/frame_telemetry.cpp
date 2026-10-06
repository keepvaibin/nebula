#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "galaxy/frame_telemetry.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

namespace galaxy::telemetry {
namespace {
std::uint64_t filetime_value(FILETIME value) noexcept {
    return (std::uint64_t{value.dwHighDateTime} << 32u) | value.dwLowDateTime;
}
}

struct Publisher::Impl {
    HANDLE mapping = nullptr;
    HANDLE guard = nullptr;
    HANDLE stop = nullptr;
    void* view = nullptr;
    Packet packet{};
    std::function<Counts()> sample;
    std::thread worker;

    Impl(std::function<Counts()> callback, std::uint64_t width,
         std::uint64_t height, std::uint64_t scale) : sample(std::move(callback)) {
        packet[0] = kMagic; packet[1] = 1; packet[2] = sizeof(Packet);
        packet[3] = GetCurrentProcessId();
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return;
        packet[4] = filetime_value(creation);
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return;
        packet[7] = static_cast<std::uint64_t>(frequency.QuadPart);
        packet[21] = width; packet[22] = height; packet[23] = scale;
        const auto name = L"Local\\Nebula.FrameTelemetry.v1." + std::to_wstring(packet[3]);
        guard = CreateMutexW(nullptr, FALSE, (name + L".Guard").c_str());
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, sizeof(Packet), name.c_str());
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!guard || !mapping || !stop) return;
        view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Packet));
        if (!view) return;
    }

    void publish(bool active) noexcept {
        LARGE_INTEGER begin{}; QueryPerformanceCounter(&begin);
        Counts counts{};
        try { counts = sample(); } catch (...) { return; }
        packet[8] = active ? 1u : 0u;
        std::copy(counts.begin(), counts.end(), packet.begin() + 9);
        packet[18] = static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
            packet[19] = filetime_value(kernel) + filetime_value(user);
        ULONG64 cycles = 0;
        if (QueryThreadCycleTime(GetCurrentThread(), &cycles)) packet[24] = cycles;
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        packet[6] = static_cast<std::uint64_t>(now.QuadPart);
        packet[20] = (std::max)(packet[20], static_cast<std::uint64_t>(
            (now.QuadPart - begin.QuadPart) * 1000000 / packet[7]));
        const DWORD acquired = WaitForSingleObject(guard, 0);
        if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) return;
        ++packet[5];
        std::memcpy(view, packet.data(), sizeof(packet));
        ReleaseMutex(guard);
    }

    void run() noexcept {
        (void)SetThreadDescription(GetCurrentThread(), L"Nebula Frame Telemetry");
        (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        while (true) {
            publish(true);
            if (WaitForSingleObject(stop, 1000) != WAIT_TIMEOUT) break;
        }
        publish(false);
    }

    ~Impl() {
        if (stop) SetEvent(stop);
        if (worker.joinable()) worker.join();
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
        if (guard) CloseHandle(guard);
        if (stop) CloseHandle(stop);
    }
};

Publisher::Publisher(std::function<Counts()> sample, std::uint64_t width,
                     std::uint64_t height, std::uint64_t scale) noexcept {
    try {
        impl_ = std::make_unique<Impl>(std::move(sample), width, height, scale);
        if (impl_->view) {
            auto* impl = impl_.get();
            impl_->worker = std::thread([impl] { impl->run(); });
        }
    } catch (...) {
        impl_.reset(); // Optional telemetry must never prevent gameplay.
    }
}
Publisher::~Publisher() = default;
}  // namespace galaxy::telemetry
