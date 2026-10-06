#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "galaxy/frame_telemetry.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() try {
    using namespace galaxy::telemetry;
    std::atomic<std::uint64_t> copies{20};
    std::unique_ptr<Publisher> publisher = std::make_unique<Publisher>([&] {
        return Counts{copies.load(), 10, 30, 20, 123, 456, 0x80001000, 100, 200};
    }, 3840, 2160, 6);
    const auto name = L"Local\\Nebula.FrameTelemetry.v1." + std::to_wstring(GetCurrentProcessId());
    const auto mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    const auto guard = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Guard").c_str());
    require(mapping && guard, "Publisher IPC objects missing");
    const auto view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Packet));
    require(view != nullptr, "Reader mapping failed");
    auto read = [&] {
        require(WaitForSingleObject(guard, 1000) == WAIT_OBJECT_0, "Reader guard failed");
        Packet result{}; std::memcpy(result.data(), view, sizeof(result));
        ReleaseMutex(guard); return result;
    };
    Packet initial{};
    for (int attempt = 0; attempt != 40; ++attempt) {
        initial = read(); if (initial[5]) break; Sleep(25);
    }
    require(initial[0] == kMagic && initial[1] == 1 && initial[2] == sizeof(Packet), "Invalid protocol header");
    require(initial[3] == GetCurrentProcessId() && initial[4] && initial[6] && initial[7], "Identity/freshness missing");
    require(initial[8] == 1 && initial[9] == 20 && initial[10] == 10 && initial[12] == 20, "Copy/first/repeat boundaries mixed");
    require(initial[21] == 3840 && initial[22] == 2160 && initial[23] == 6, "Initial display settings missing");
    require(WaitForSingleObject(guard, 1000) == WAIT_OBJECT_0, "Test guard failed");
    copies.store(25); Sleep(1200); // A held reader must never block the producer/game.
    Packet held{}; std::memcpy(held.data(), view, sizeof(held));
    require(held[5] == initial[5], "Publisher wrote through held reader guard");
    ReleaseMutex(guard);
    Sleep(1100);
    const auto next = read();
    require(next[5] > initial[5] && next[6] > initial[6] && next[9] == 25 && next[10] == 10, "Missed publication recovery/count coherence failed");
    publisher.reset();
    require(read()[8] == 0, "Normal shutdown did not invalidate retained reader mapping");
    UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(guard);
    std::cout << "PASS: separate counts, process identity, reader contention, resumed publication and shutdown freshness\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
