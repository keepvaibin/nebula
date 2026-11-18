// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A DXGI adapter's video memory (Direct3D 12 and 11): its use and budget by the renderers' one rule
// (video_memory_reading.h), and the OS's word that the budget moved (RegisterVideoMemoryBudgetChangeNotificationEvent,
// an auto-reset event), which makes the next read ask at once.

#include "galaxy/render/video_memory_reading.h"
#include "galaxy/windows_api.h"

#include <dxgi1_4.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <iostream>

namespace galaxy::render {

class DxgiMemory {
public:
    DxgiMemory() = default;
    DxgiMemory(const DxgiMemory&) = delete;
    DxgiMemory& operator=(const DxgiMemory&) = delete;
    ~DxgiMemory() { release(); }

    // The adapter the device was made on (null: none). The same adapter again changes nothing.
    void attach(IDXGIAdapter* adapter) noexcept {
        if (adapter == attached_) {
            return;
        }
        release();
        attached_ = adapter;
        if (adapter == nullptr || FAILED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3_)))) {
            adapter3_.Reset();
            return;
        }
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event_ != nullptr && FAILED(adapter3_->RegisterVideoMemoryBudgetChangeNotificationEvent(event_, &cookie_))) {
            cookie_ = 0u;
            CloseHandle(event_);
            event_ = nullptr;
        }
    }

    void release() noexcept {
        if (adapter3_ && cookie_ != 0u) {
            adapter3_->UnregisterVideoMemoryBudgetChangeNotification(cookie_);
        }
        cookie_ = 0u;
        if (event_ != nullptr) {
            CloseHandle(event_);
            event_ = nullptr;
        }
        adapter3_.Reset();
        attached_ = nullptr;
        reading_.forget();
    }

    // The use and budget by the rule (at most every 100 ms; at once when the OS moved the budget, which is said).
    bool read(std::uint64_t& usage, std::uint64_t& budget) noexcept {
        usage = budget = 0u;
        if (!adapter3_) {
            return false;
        }
        const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()).count();
        const bool moved = event_ != nullptr && WaitForSingleObject(event_, 0u) == WAIT_OBJECT_0;
        const std::uint64_t before = reading_.budget();
        const bool known = reading_.read(now, moved, [this](std::uint64_t& u, std::uint64_t& b) { return ask(u, b); },
                                         usage, budget);
        if (moved && known && before != 0u && budget != before) {
            std::cout << "[vram] the OS moved the budget from " << (before >> 20u) << " MB to " << (budget >> 20u)
                      << " MB\n";
        }
        return known;
    }

    // The driver's figures now, for a decision that cannot use figures 100 ms old (a new scene size).
    bool read_now(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
        usage = budget = 0u;
        return adapter3_ && ask(usage, budget) && budget != 0u;
    }

private:
    bool ask(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (FAILED(adapter3_->QueryVideoMemoryInfo(0u, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
            return false;
        }
        usage = info.CurrentUsage;
        budget = info.Budget;
        return true;
    }

    IDXGIAdapter* attached_ = nullptr;  // compared only, never used: the adapter3_ reference keeps it alive
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
    HANDLE event_ = nullptr;
    DWORD cookie_ = 0u;
    VideoMemoryReading reading_;
};

}  // namespace galaxy::render
