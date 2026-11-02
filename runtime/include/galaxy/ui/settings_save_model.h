// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace galaxy::ui {

// The host owns persistence and thread naming; the model owns retry and scheduling.
class SettingsSaveModel {
public:
    using Writer = bool (*)();
    using Notify = void (*)();
    SettingsSaveModel(Writer runtime, Writer render, Notify failed, Notify started = nullptr)
        : runtime_(runtime), render_(render), failed_(failed), started_(started) {}

    std::atomic<bool> dirty{false};
    std::atomic<bool> error{false};
    bool write();
    bool queue();
    bool pending();
    void save_now();
    void wait_until_idle();

private:
    void run();
    Writer runtime_;
    Writer render_;
    Notify failed_;
    Notify started_;
    std::once_flag worker_started_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool requested_ = false;
    bool running_ = false;
};

}  // namespace galaxy::ui
