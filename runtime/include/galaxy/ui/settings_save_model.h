// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace galaxy::ui {

// The host owns persistence and thread naming; the model owns retry and scheduling. Its one thread writes the
// settings and, once watch() starts it, looks at the files for changes made outside the game between saves: no other
// thread (the render thread among them) touches the settings files.
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
    // `look` runs on the model's thread every `period` while no save is asked for or running (never during one).
    // Once; later calls do nothing.
    void watch(Notify look, std::chrono::milliseconds period);

private:
    void run();
    Writer runtime_;
    Writer render_;
    Notify failed_;
    Notify started_;
    void start_worker();
    std::once_flag worker_started_;
    Notify look_ = nullptr;  // under mutex_
    std::chrono::milliseconds look_every_{0};
    std::mutex mutex_;
    std::condition_variable changed_;
    bool requested_ = false;
    bool running_ = false;
};

}  // namespace galaxy::ui
