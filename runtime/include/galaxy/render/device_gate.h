// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A lost graphics card: the render thread remakes everything on the card while the game goes on. Services run on any
// thread, so they hold this gate while they work on the device. A service the loss interrupts waits for the new card
// and does its work again; a free waits, so what is being read back from the game's memory stays there.

#include "galaxy/crash_report.h"
#include "galaxy/render/gpu_types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <shared_mutex>

namespace galaxy::render {

class DeviceGate {
public:
    void set_device(gpu::Device* device) noexcept { device_.store(device, std::memory_order_release); }

    template <class Work>
    auto use(Work&& work) -> decltype(work()) {
        for (;;) {
            std::exception_ptr failure;
            std::uint64_t seen = 0u;
            {
                const std::shared_lock<std::shared_mutex> read(gate_);
                seen = epoch_.load(std::memory_order_acquire);
                try {
                    return work();
                } catch (const std::exception& error) {
                    if (!crash::is_gpu_lost(error.what()) && !gpu::device_removed(device_.load(std::memory_order_acquire))) {
                        throw;
                    }
                    failure = std::current_exception();
                }
            }
            lost_seen_.store(true, std::memory_order_release);
            std::unique_lock<std::mutex> wait(wait_lock_);
            back_.wait(wait, [&] { return gave_up_ || epoch_.load(std::memory_order_acquire) != seen; });
            if (gave_up_) {
                std::rethrow_exception(failure);
            }
        }
    }

    [[nodiscard]] std::shared_lock<std::shared_mutex> hold() { return std::shared_lock<std::shared_mutex>(gate_); }
    // Moves each time a new card is ready. Work taken under one epoch means nothing under another.
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }
    // A service met the loss before the render thread did.
    [[nodiscard]] bool lost_seen() const noexcept { return lost_seen_.load(std::memory_order_acquire); }

    [[nodiscard]] std::unique_lock<std::shared_mutex> begin_rebuild() { return std::unique_lock<std::shared_mutex>(gate_); }
    void end_rebuild(std::unique_lock<std::shared_mutex>& rebuilding) {
        {
            const std::lock_guard<std::mutex> guard(wait_lock_);
            epoch_.fetch_add(1u, std::memory_order_acq_rel);
            lost_seen_.store(false, std::memory_order_release);
        }
        rebuilding.unlock();
        back_.notify_all();
    }
    // No card is coming back: whoever waits ends in its own error.
    void give_up() {
        {
            const std::lock_guard<std::mutex> guard(wait_lock_);
            gave_up_ = true;
        }
        back_.notify_all();
    }

private:
    std::shared_mutex gate_;
    std::mutex wait_lock_;
    std::condition_variable back_;
    std::atomic<gpu::Device*> device_{nullptr};
    std::atomic<std::uint64_t> epoch_{0u};
    std::atomic<bool> lost_seen_{false};
    bool gave_up_ = false;
};

}  // namespace galaxy::render
