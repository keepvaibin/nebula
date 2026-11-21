#include "galaxy/windows_api.h"
#include "galaxy/frame_telemetry.h"
#include "galaxy/test_wait.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The publisher's thread runs below the normal priority and publishes once a second: every wait here is for what it has
// done (its samples, its serial), up to galaxy::kTestWaitDeadline, never for a time (N-1073).
constexpr DWORD kGuardWaitMs = static_cast<DWORD>(galaxy::kTestWaitDeadline.count() * 1000);

int main() try {
    using namespace galaxy::telemetry;
    std::atomic<std::uint64_t> copies{20};
    std::atomic<unsigned> samples{0};  // the publisher's looks at the counts: one for each publication it begins
    std::unique_ptr<Publisher> publisher = std::make_unique<Publisher>([&] {
        samples.fetch_add(1);
        return Counts{copies.load(), 10, 30, 20, 123, 456, 0x70001000, 100, 200};
    }, 3840, 2160, 6);
    const auto name = L"Local\\Nebula.FrameTelemetry.v1." + std::to_wstring(GetCurrentProcessId());
    const auto mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    const auto guard = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L".Guard").c_str());
    require(mapping && guard, "Publisher IPC objects missing");
    const auto view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Packet));
    require(view != nullptr, "Reader mapping failed");
    auto read = [&] {
        require(WaitForSingleObject(guard, kGuardWaitMs) == WAIT_OBJECT_0, "Reader guard never came");
        Packet result{}; std::memcpy(result.data(), view, sizeof(result));
        ReleaseMutex(guard); return result;
    };
    Packet initial{};
    require(galaxy::test_wait([&] { initial = read(); return initial[5] != 0; }), "The first publication never came");
    require(initial[0] == kMagic && initial[1] == 1 && initial[2] == sizeof(Packet), "Invalid protocol header");
    require(initial[3] == GetCurrentProcessId() && initial[4] && initial[6] && initial[7], "Identity/freshness missing");
    require(initial[8] == 1 && initial[9] == 20 && initial[10] == 10 && initial[12] == 20, "Copy/first/repeat boundaries mixed");
    require(initial[21] == 3840 && initial[22] == 2160 && initial[23] == 6, "Initial display settings missing");
    // A held reader must never block the producer/game: while the test holds the guard, the publisher takes a new sample
    // and cannot write. A sample begins only after the publication before it has ended, so the second sample after the
    // hold began shows one whole publication that met the held guard.
    require(WaitForSingleObject(guard, kGuardWaitMs) == WAIT_OBJECT_0, "Test guard never came");
    Packet held_at{}; std::memcpy(held_at.data(), view, sizeof(held_at));
    const unsigned at_hold = samples.load();
    copies.store(25);
    require(galaxy::test_wait([&] { return samples.load() >= at_hold + 2; }), "The publisher made no publication during the hold");
    Packet held{}; std::memcpy(held.data(), view, sizeof(held));
    require(held[5] == held_at[5], "Publisher wrote through held reader guard");
    ReleaseMutex(guard);
    Packet next{};
    require(galaxy::test_wait([&] { next = read(); return next[5] > held_at[5]; }), "The publication never resumed");
    require(next[5] > initial[5] && next[6] > initial[6] && next[9] == 25 && next[10] == 10, "Missed publication recovery/count coherence failed");
    publisher.reset();
    require(read()[8] == 0, "Normal shutdown did not invalidate retained reader mapping");
    UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(guard);
    std::cout << "PASS: separate counts, process identity, reader contention, resumed publication and shutdown freshness\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
