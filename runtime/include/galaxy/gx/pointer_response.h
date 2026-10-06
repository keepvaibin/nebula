#pragma once
#include <cstdint>
#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace galaxy::gx {
// One reusable scene-depth result for mouse picking. The render owner writes
// pixels and status before releasing `complete`; the guest owner reads only
// after acquiring it. Shared ownership keeps it alive across queued utility
// work and renderer failure.
struct PointerDepthCapture {
    std::array<std::uint32_t,640u*528u> pixels{};
    std::atomic<bool> complete{true};
    bool success{};
    std::exception_ptr error;
    std::uint64_t worker_ns{};
    // Mouse acquisition never waits. A frame consumer may wait for an already
    // enqueued result. Completion is published under the same mutex as the wait
    // predicate to prevent a lost wakeup; ready checks take no lock.
    std::mutex completion_mutex;
    std::condition_variable completion_cv;
    void publish_complete() noexcept {
        {
            const std::lock_guard<std::mutex> lock(completion_mutex);
            complete.store(true,std::memory_order_release);
        }
        completion_cv.notify_all();
    }
    [[nodiscard]] bool await_until(std::chrono::steady_clock::time_point deadline) {
        if (complete.load(std::memory_order_acquire)) return true;
        std::unique_lock<std::mutex> lock(completion_mutex);
        return completion_cv.wait_until(lock,deadline,[this] {
            return complete.load(std::memory_order_acquire);
        });
    }
};
struct PointerResponseStamp {
    std::uint64_t serial{}, generation{}, selected_ns{}, processed_ns{}, drawn_ns{};
    std::uint32_t x_bits{}, y_bits{};
    // GetTickCount64 domain (millisecond resolution). Measures acquisition age,
    // not physical input latency.
    std::uint64_t acquisition_age_ms{};
    bool acquisition_age_known{};
};
}
