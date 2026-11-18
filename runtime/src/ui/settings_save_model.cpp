// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/ui/settings_save_model.h"

#include <chrono>
#include <thread>

namespace galaxy::ui {

bool SettingsSaveModel::write() {
    if (!dirty.exchange(false, std::memory_order_acq_rel)) return true;
    bool runtime_saved = false;
    bool render_saved = false;
    try { runtime_saved = runtime_(); } catch (...) {}
    try { render_saved = render_(); } catch (...) {}
    if (!runtime_saved || !render_saved) {
        dirty.store(true, std::memory_order_release);
        if (!error.exchange(true, std::memory_order_acq_rel) && failed_) failed_();
        return false;
    }
    error.store(false, std::memory_order_release);
    return true;
}

bool SettingsSaveModel::queue() {
    if (!dirty.load(std::memory_order_acquire)) return true;
    start_worker();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        requested_ = true;
    }
    changed_.notify_all();
    return true;
}

void SettingsSaveModel::start_worker() {
    std::call_once(worker_started_, [this] { std::thread([this] { run(); }).detach(); });
}

void SettingsSaveModel::watch(Notify look, std::chrono::milliseconds period) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (look_ != nullptr || look == nullptr) return;
        look_ = look;
        look_every_ = period;
    }
    start_worker();
    changed_.notify_all();
}

void SettingsSaveModel::run() {
    if (started_) started_();
    auto next_look = std::chrono::steady_clock::now();
    for (;;) {
        Notify look = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (look_ == nullptr) {
                changed_.wait(lock, [this] { return requested_ || look_ != nullptr; });
            } else {
                changed_.wait_until(lock, next_look, [this] { return requested_; });
            }
            if (!requested_) {
                if (look_ != nullptr && std::chrono::steady_clock::now() >= next_look) {
                    look = look_;
                    next_look = std::chrono::steady_clock::now() + look_every_;
                }
            }
        }
        if (look != nullptr) {
            try { look(); } catch (...) {}
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!requested_) continue;
            requested_ = false;
            running_ = true;
        }
        (void)write();
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        changed_.notify_all();
    }
}

bool SettingsSaveModel::pending() {
    const std::lock_guard<std::mutex> lock(mutex_);
    return requested_ || running_;
}

void SettingsSaveModel::save_now() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock, std::chrono::seconds(5), [this] { return !requested_ && !running_; });
    }
    (void)write();
}

}  // namespace galaxy::ui
