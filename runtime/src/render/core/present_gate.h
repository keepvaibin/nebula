// SPDX-License-Identifier: GPL-3.0-only
// Direct3D 12: no pipeline creation spans a Present. NVIDIA's D3D12 driver (nvwgf2umx.dll, GeForce 610.88) trims a
// device-wide cache of compiled pipelines inside Present, and full dumps of cold starts show it freeing entries that a
// creation in flight on another thread still held (an access violation in the driver, with 10 to 30 creations in
// flight). Direct3D 12 documents both calls as free to overlap, so this is a guard against that driver, not a rule of
// the API: a Present waits for the creations already in flight and runs alone; a creation waits while a Present is
// due or running. Outside the warm-up no creation is in flight and a Present only takes and drops one lock.
#pragma once

#include <condition_variable>
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
    void begin_present() {
        std::unique_lock held(lock_);
        ++presents_;
        turn_.wait(held, [this] { return creations_ == 0u; });
    }
    void end_present() {
        {
            const std::lock_guard held(lock_);
            --presents_;
        }
        turn_.notify_all();
    }

private:
    std::mutex lock_;
    std::condition_variable turn_;
    unsigned creations_ = 0u;
    unsigned presents_ = 0u;
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
