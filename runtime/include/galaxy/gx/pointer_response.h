#pragma once
// Scene-depth captures for mouse picking and the timing stamp of a pointer response.
#include <cstdint>
#include <array>
#include <atomic>
#include <exception>
#include <condition_variable>
#include <mutex>

namespace galaxy::gx {
// A reusable scene-depth result for mouse picking. The render thread writes the pixels and status, then releases
// `complete`; the reader acquires it first. Held by shared_ptr so it outlives queued work and renderer failure.
struct PointerDepthCapture {
    std::array<std::uint32_t,640u*528u> pixels{};
    // Asynchronous render path: the field in the capture ring's mapped readback buffer (640x528 RGBA8, contiguous
    // rows), converted per queried pixel. Null when the result is in `pixels`.
    const std::uint8_t* rgba{};
    std::atomic<bool> complete{true};
    bool success{};
    std::exception_ptr error;
    std::uint64_t worker_ns{};
    // Mouse acquisition never waits; a frame consumer may wait for a queued result. `complete` is set under the
    // mutex so a waiter cannot miss the wakeup; ready checks take no lock.
    std::mutex completion_mutex;
    std::condition_variable completion_cv;
    void publish_complete() noexcept {
        {
            const std::lock_guard<std::mutex> lock(completion_mutex);
            complete.store(true,std::memory_order_release);
        }
        completion_cv.notify_all();
    }
};
struct PointerResponseStamp {
    std::uint64_t serial{}, generation{}, selected_ns{}, processed_ns{}, drawn_ns{};
    // GetTickCount64 milliseconds: the age of the pointer sample, not input latency.
    std::uint64_t acquisition_age_ms{};
    bool acquisition_age_known{};
};
}
