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
    std::call_once(worker_started_, [this] { std::thread([this] { run(); }).detach(); });
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        requested_ = true;
    }
    changed_.notify_all();
    return true;
}

void SettingsSaveModel::run() {
    if (started_) started_();
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] { return requested_; });
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

void SettingsSaveModel::wait_until_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [this] { return !requested_ && !running_; });
}

}  // namespace galaxy::ui
