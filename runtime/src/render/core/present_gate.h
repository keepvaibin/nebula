// SPDX-License-Identifier: GPL-3.0-only
// Direct3D 12: no pipeline creation spans a Present. NVIDIA's D3D12 driver (nvwgf2umx.dll, GeForce 610.88) trims a
// device-wide cache of compiled pipelines inside Present, and full dumps of cold starts show it freeing entries that a
// creation in flight on another thread still held (an access violation in the driver, with 10 to 30 creations in
// flight). Direct3D 12 documents both calls as free to overlap, so this is a guard against that driver, not a rule of
// the API: a Present waits for the creations already in flight and runs alone; a creation waits while a Present is
// due or running. The pipelines are made at the start and while a load waits for them (pipelines.h, set_hurried); in
// play none is in flight unless a draw missed one or a setting changed, and a Present only takes and drops one lock.
// Only Direct3D 12's creations take the gate (pipelines_d3d12.cpp): Vulkan, Direct3D 11 and Metal have none.
#pragma once

#include "galaxy/frame_timeline.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace galaxy::render::core {

class PresentGate {
public:
    void begin_creation() {
        std::unique_lock held(lock_);
        turn_.wait(held, [this] { return presents_ == 0u; });
        ++creations_;
    }
    void end_creation() {
        bool last = false;
        {
            const std::lock_guard held(lock_);
            last = --creations_ == 0u;
        }
        if (last) {
            turn_.notify_all();
        }
    }
    // presents_ counts the Present due and the one running: new creations wait from the moment one is due.
    // A wait is a timeline event (--diagnostics timeline): a tour's timeline names each Present a creation held. A wait in
    // play (after the first picture with a scene, no load waiting for its pipelines: set_in_play) is also a log line.
    void begin_present() {
        std::unique_lock held(lock_);
        ++presents_;
        if (creations_ == 0u) {
            return;
        }
        const unsigned creations = creations_;
        const auto began = std::chrono::steady_clock::now();
        ::galaxy::timeline::record(::galaxy::timeline::Event::GateWaitBegin, creations);
        turn_.wait(held, [this] { return creations_ == 0u; });
        ::galaxy::timeline::record(::galaxy::timeline::Event::GateWaitEnd);
        held.unlock();
        if (in_play_.load(std::memory_order_relaxed) && play_waits_.fetch_add(1u, std::memory_order_relaxed) < kPlayWaitLines) {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began).count();
            std::fprintf(stderr, "[present] a Present in play waited %lld us for %u pipeline creations in flight\n",
                         static_cast<long long>(us), creations);
        }
    }
    // The pipelines (Pipelines::picture_shown, set_hurried, initialize): true from the first picture with a scene, false
    // while a load waits for its pipelines and on a new device.
    void set_in_play(bool in_play) noexcept { in_play_.store(in_play, std::memory_order_relaxed); }
    [[nodiscard]] unsigned play_waits() const noexcept { return play_waits_.load(std::memory_order_relaxed); }
    // A Present is held by a creation in flight now.
    [[nodiscard]] bool present_held() {
        const std::lock_guard held(lock_);
        return presents_ != 0u && creations_ != 0u;
    }
    void end_present() {
        {
            const std::lock_guard held(lock_);
            --presents_;
        }
        turn_.notify_all();
    }

private:
    static constexpr unsigned kPlayWaitLines = 100u;  // then counted only (play_waits, the timeline)
    std::mutex lock_;
    std::condition_variable turn_;
    unsigned creations_ = 0u;
    unsigned presents_ = 0u;
    std::atomic<bool> in_play_{false};
    std::atomic<unsigned> play_waits_{0u};
};

// The one gate of the process: the device's pipeline workers and the render thread's Present.
inline PresentGate gPresentGate;

struct CreationScope {
    CreationScope() { gPresentGate.begin_creation(); }
    ~CreationScope() { gPresentGate.end_creation(); }
    CreationScope(const CreationScope&) = delete;
    CreationScope& operator=(const CreationScope&) = delete;
};

struct PresentScope {
    PresentScope() { gPresentGate.begin_present(); }
    ~PresentScope() { gPresentGate.end_present(); }
    PresentScope(const PresentScope&) = delete;
    PresentScope& operator=(const PresentScope&) = delete;
};

}  // namespace galaxy::render::core
