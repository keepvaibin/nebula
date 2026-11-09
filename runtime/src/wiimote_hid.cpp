// The real Wii Remote's worker (wiimote_hid.h): finds a remote through the platform's link (wiimote_link.h), sets it
// up as the game wants it and hands its reports to the host's input thread.
#include "galaxy/wiimote_hid.h"
#include "galaxy/launch_args.h"

#include "galaxy/input/host_input.h"
#include "galaxy/platform_time.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/wiimote_link.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#endif

namespace galaxy::host {

namespace {

namespace wm = input::wiimote;
namespace wl = wiimote_link;

struct Shared {
    RealWiimoteHandoff handoff;
    // Host to remote: what the game asks for; a change wakes the worker (wl::wake).
    std::atomic<std::uint8_t> report{0x30};
    std::atomic<bool> ir{false};
    std::atomic<bool> rumble{false};
    // For the settings (wii_remote_state): 1 connected, 2 Nunchuk, 4 has a MotionPlus, 8 MotionPlus on.
    std::atomic<std::uint8_t> status{0};
    std::once_flag started;
};

// Never destroyed: the worker may still be blocked on the device when the process exits.
Shared& shared() {
    static Shared* instance = new Shared;
    return *instance;
}

bool remote_wanted() {
    const RuntimeInputMode mode = get_runtime_input_mode_state().mode;
    return mode == RuntimeInputMode::Auto || is_wii_remote_mode(mode);
}

// The motion pointer mode: the MotionPlus on (a change sets the remote up again).
bool motion_plus_wanted() {
    return get_runtime_input_mode_state().mode == RuntimeInputMode::WiimoteMotion;
}

class Worker {
public:
    void run() {
        wl::start();
        // Looked for when the input mode allows a remote: at once, when a device arrives, and again after a pause that
        // grows to a minute while none answers.
        bool wanted = false;
        bool arrived = false;
        std::uint64_t next_scan_ms = 0;
        std::uint64_t backoff_ms = 2'000;
        for (;;) {
            const bool now_wanted = remote_wanted();
            if (now_wanted && !wanted) next_scan_ms = 0;
            wanted = now_wanted;
            arrived = wl::wait_arrival(0) || arrived;
            if (wanted && (arrived || steady_ms() >= next_scan_ms)) {
                arrived = false;
                bool answered = false;
                wl::each_remote([&](wl::Link& remote) {
                    link_ = &remote;
                    wl::set_serving(true);
                    if (initialise()) {
                        answered = true;
                        serve();
                    }
                    wl::set_serving(false);
                    if (published_) publish_disconnected();
                    link_ = nullptr;
                    return answered;
                });
                if (answered) {
                    backoff_ms = 2'000;
                    next_scan_ms = 0;
                    continue;
                }
                next_scan_ms = steady_ms() + backoff_ms;
                backoff_ms = std::min<std::uint64_t>(backoff_ms * 2u, 60'000);
            }
            // The mode is looked at again within a second; an arrival wakes it at once (and is seen above).
            arrived = wl::wait_arrival(1000) || arrived;
        }
    }

private:
    using Bytes = std::array<std::uint8_t, wm::kOutputBytes>;

    bool send(std::size_t used, const Bytes& out) {
        return used != 0u && link_->send(std::span<const std::uint8_t>(out.data(), used));
    }

    void publish_connected() {
        shared().status.store(static_cast<std::uint8_t>(1u | (connection_.nunchuk ? 2u : 0u) |
                                                         (connection_.has_motion_plus ? 4u : 0u) |
                                                         (connection_.motion_plus ? 8u : 0u)),
                              std::memory_order_release);
        shared().handoff.connected(connection_);
        published_ = true;
        published_battery_ = connection_.battery;
        std::cerr << "[wiimote] connected, nunchuk " << (connection_.nunchuk ? "yes" : "no") << ", motionplus "
                  << (connection_.motion_plus ? "on" : connection_.has_motion_plus ? "off" : "none") << ", battery level "
                  << static_cast<unsigned>(connection_.battery) << '\n';
    }

    void publish_disconnected() {
        shared().status.store(0u, std::memory_order_release);
        shared().handoff.disconnected();
        published_ = false;
        std::cerr << "[wiimote] disconnected\n";
    }

    // One input report outside a request: data to the game, a status noted.
    void take(std::span<const std::uint8_t> report) {
        if (report.empty()) return;
        if (wm::is_data_report(report[0])) {
            // With the MotionPlus on, its samples say whether a Nunchuk is plugged into it.
            if (connection_.motion_plus && report[0] == wm::kMotionPlusReport && report.size() >= 12u &&
                (report[11] & 0x02u) != 0u) {
                const bool port = (report[10] & 0x01u) != 0u;
                if (port != passthrough_port_) passthrough_changed_ = true;
            }
            shared().handoff.report(report);
        } else if (const wm::Status status = wm::parse_status(report); status.valid) {
            status_ = status;
            status_pending_ = true;
        }
    }

    // Sends `out` and reads until `match` takes a report or `timeout_ms` passes; other reports are taken.
    template <class Match>
    bool request(std::size_t used, const Bytes& out, std::uint32_t timeout_ms, Match match) {
        if (!send(used, out)) return false;
        const std::uint64_t deadline = steady_ms() + timeout_ms;
        for (std::uint64_t now = steady_ms(); now < deadline; now = steady_ms()) {
            const int size = link_->receive(static_cast<std::uint32_t>(deadline - now), false);
            if (size < 0) {
                gone_ = true;
                return false;
            }
            if (size == 0) continue;
            const std::span<const std::uint8_t> report = link_->data(size);
            if (match(report)) return true;
            take(report);
        }
        return false;
    }

    // `size` bytes at `address`, from the replies whose 16-byte blocks fall inside them (a late reply to an earlier
    // read is ignored).
    bool read_memory(std::uint8_t space, std::uint32_t address, std::uint16_t size, std::uint8_t* out) {
        Bytes request_bytes{};
        const std::size_t used = wm::build_read(space, address, size, rumble_, request_bytes);
        const std::size_t blocks = (size + 15u) / 16u;
        std::vector<bool> got(blocks, false);
        std::size_t received = 0;
        bool failed = false;
        const bool done = request(used, request_bytes, 600, [&](std::span<const std::uint8_t> report) {
            const wm::ReadReply reply = wm::parse_read_reply(report);
            if (!reply.valid) return false;
            const std::size_t offset = static_cast<std::uint16_t>(reply.address - static_cast<std::uint16_t>(address));
            if (offset >= size || offset % 16u != 0u || got[offset / 16u]) return false;
            if (reply.error != 0u) {
                failed = true;
                return true;
            }
            got[offset / 16u] = true;
            for (std::size_t i = 0; i < reply.data.size() && offset + i < size; ++i) out[offset + i] = reply.data[i];
            return ++received == blocks;
        });
        return done && !failed;
    }

    bool write_register(std::uint32_t address, std::span<const std::uint8_t> data) {
        Bytes out{};
        const std::size_t used = wm::build_write(wm::kRegisters, address, data, rumble_, out);
        wm::Ack ack;
        const bool done = request(used, out, 300, [&](std::span<const std::uint8_t> report) {
            ack = wm::parse_ack(report);
            return ack.valid && ack.report == wm::kOutWrite;
        });
        return done && ack.error == 0u;
    }

    // Reads for `ms`, taking what comes (an extension's change answers with a status report).
    void drain(std::uint32_t ms) {
        const std::uint64_t deadline = steady_ms() + ms;
        for (std::uint64_t now = steady_ms(); now < deadline && !gone_; now = steady_ms()) {
            const int size = link_->receive(static_cast<std::uint32_t>(deadline - now), false);
            if (size < 0) {
                gone_ = true;
                return;
            }
            if (size > 0) take(link_->data(size));
        }
    }

    // A MotionPlus the remote kept on since an earlier connection is turned off, so the extension behind it shows.
    void motion_plus_left_on() {
        if (!extension_) return;
        std::array<std::uint8_t, 6> id{};
        if (!read_memory(wm::kRegisters, wm::kExtensionId, static_cast<std::uint16_t>(id.size()), id.data()) ||
            !wm::is_active_motion_plus(id)) {
            return;
        }
        const std::uint8_t off = 0x55;
        (void)write_register(wm::kMotionPlusOff, std::span(&off, 1));
        drain(400);
        if (status_pending_) {
            status_pending_ = false;
            extension_ = status_.extension;
        }
    }

    // The MotionPlus on, passing the Nunchuk's reports on when one is in its port; its calibration. A step that fails
    // leaves it off (the remote then points at the sensor bar).
    void activate_motion_plus() {
        const std::uint8_t init = 0x55;
        const std::uint8_t mode = connection_.nunchuk ? wm::kMotionPlusNunchuk : wm::kMotionPlusAlone;
        if (!write_register(wm::kMotionPlusInit, std::span(&init, 1)) ||
            !write_register(wm::kMotionPlusMode, std::span(&mode, 1))) {
            return;
        }
        drain(400);
        status_pending_ = false;  // the MotionPlus arriving in the extension's place, not a new extension
        std::array<std::uint8_t, 6> id{};
        if (!read_memory(wm::kRegisters, wm::kExtensionId, static_cast<std::uint16_t>(id.size()), id.data()) ||
            !wm::is_active_motion_plus(id)) {
            return;
        }
        connection_.motion_plus = read_memory(wm::kRegisters, wm::kExtensionCalibration,
                                              static_cast<std::uint16_t>(connection_.motion_plus_calibration.size()),
                                              connection_.motion_plus_calibration.data());
        extension_ = true;
        motion_plus_mode_ = mode;
        passthrough_port_ = connection_.nunchuk;
        passthrough_changed_ = false;
    }

    // The extensions as the input mode wants them: a MotionPlus left on goes off, the Nunchuk is read, then the
    // MotionPlus is looked for and, in the motion pointer mode, turned on.
    void setup_extensions() {
        connection_.motion_plus = false;
        connection_.has_motion_plus = false;
        connection_.motion_plus_calibration.fill(0);
        motion_plus_left_on();
        setup_extension(extension_);
        std::array<std::uint8_t, 6> id{};
        connection_.has_motion_plus =
            read_memory(wm::kRegisters, wm::kMotionPlusInactiveId, static_cast<std::uint16_t>(id.size()), id.data()) &&
            wm::is_inactive_motion_plus(id);
        if (connection_.has_motion_plus && motion_wanted_) activate_motion_plus();
    }

    // A Nunchuk plugged into the MotionPlus, or taken out: the passthrough mode it needs, and its id and calibration
    // as the MotionPlus keeps them (id at 0xA400F6: its first and last bytes are 0 for a Nunchuk).
    void passthrough_changed() {
        passthrough_changed_ = false;
        passthrough_port_ = !passthrough_port_;
        connection_.nunchuk = false;
        connection_.extension_calibration.fill(0);
        if (passthrough_port_) {
            if (motion_plus_mode_ != wm::kMotionPlusNunchuk) {
                const std::uint8_t mode = wm::kMotionPlusNunchuk;
                if (write_register(wm::kMotionPlusActiveMode, std::span(&mode, 1))) motion_plus_mode_ = mode;
                drain(400);
                status_pending_ = false;
            }
            std::array<std::uint8_t, 4> id{};
            connection_.nunchuk =
                motion_plus_mode_ == wm::kMotionPlusNunchuk &&
                read_memory(wm::kRegisters, 0xa400f6, static_cast<std::uint16_t>(id.size()), id.data()) && id[0] == 0u &&
                id[3] == 0u &&
                read_memory(wm::kRegisters, wm::kPassthroughCalibration, wm::kPassthroughCalibrationBytes,
                            connection_.extension_calibration.data());
        }
        publish_connected();
        apply_report();
    }

    // The Nunchuk unencrypted, its id and calibration; another extension (or none) reads as none.
    void setup_extension(bool attached) {
        connection_.nunchuk = false;
        connection_.extension_calibration.fill(0);
        if (!attached) return;
        const std::uint8_t first = 0x55, second = 0x00;
        std::array<std::uint8_t, 6> id{};
        if (!write_register(wm::kExtensionSetup1, std::span(&first, 1)) ||
            !write_register(wm::kExtensionSetup2, std::span(&second, 1)) ||
            !read_memory(wm::kRegisters, wm::kExtensionId, static_cast<std::uint16_t>(id.size()), id.data())) {
            return;
        }
        connection_.nunchuk = wm::is_nunchuk(id) &&
                              read_memory(wm::kRegisters, wm::kExtensionCalibration, wm::kExtensionCalibrationBytes,
                                          connection_.extension_calibration.data());
    }

    // The game's report, or the MotionPlus's while it is on.
    std::uint8_t wanted_report() const {
        return connection_.motion_plus ? wm::kMotionPlusReport : shared().report.load(std::memory_order_acquire);
    }

    void apply_report() {
        Bytes out{};
        report_ = wanted_report();
        (void)send(wm::build_report_mode(report_, true, rumble_, out), out);
    }

    // The camera on in the mode the report carries, or off. A step that fails leaves the mode unknown, so the serve
    // loop sets the camera up again.
    void apply_ir() {
        const bool on = shared().ir.load(std::memory_order_acquire);
        const std::uint8_t mode = on ? wm::ir_mode_for_report(report_) : 0u;
        Bytes out{};
        bool ok = send(wm::build_enable(wm::kOutIrClock, mode != 0u, rumble_, out), out);
        ok = send(wm::build_enable(wm::kOutIrLogic, mode != 0u, rumble_, out), out) && ok;
        if (mode != 0u) {
            for (const wm::RegisterWrite& write : wm::ir_setup(mode)) {
                ok = ok && write_register(write.address, std::span<const std::uint8_t>(write.data.data(), write.size));
            }
        }
        ir_mode_ = ok ? mode : kIrUnknown;
    }

    bool initialise() {
        gone_ = false;
        status_pending_ = false;
        rumble_ = false;
        Bytes out{};
        wm::Status status;
        if (!request(wm::build_status_request(false, out), out, 1000, [&](std::span<const std::uint8_t> report) {
                status = wm::parse_status(report);
                return status.valid;
            })) {
            return false;
        }
        (void)send(wm::build_leds(0x01, false, out), out);
        connection_ = {};
        connection_.battery = wm::battery_level(status.battery);
        if (!read_memory(wm::kEeprom, wm::kCalibrationAddress, wm::kCalibrationBytes, connection_.eeprom.data())) {
            return false;
        }
        extension_ = status.extension;
        motion_wanted_ = motion_plus_wanted();
        setup_extensions();
        if (gone_) return false;
        publish_connected();
        apply_report();
        apply_ir();
        status_asked_ms_ = steady_ms();
        return !gone_;
    }

    void serve() {
        Shared& s = shared();
        while (!gone_ && remote_wanted()) {
            // The motion pointer mode turned on or off: the remote is set up again (the run loop opens it again).
            if (connection_.has_motion_plus && motion_plus_wanted() != motion_wanted_) return;
            const int size = link_->receive(1000, true);
            if (size < 0) return;
            if (size > 0) take(link_->data(size));
            // The battery once a minute (the game warns when it runs low).
            if (steady_ms() - status_asked_ms_ >= 60'000u) {
                status_asked_ms_ = steady_ms();
                Bytes out{};
                (void)send(wm::build_status_request(rumble_, out), out);
            }
            if (status_pending_) {
                // Its battery; maybe an extension in or out (the remote then reports only once its mode is set again,
                // so the mode is sent after every status; the camera keeps its setup).
                status_pending_ = false;
                const std::uint8_t battery = wm::battery_level(status_.battery);
                connection_.battery = battery;
                // An active MotionPlus is always the extension: its port's changes come in its samples.
                if (!connection_.motion_plus && status_.extension != extension_) {
                    extension_ = status_.extension;
                    setup_extensions();
                    publish_connected();
                } else if (battery != published_battery_) {
                    shared().handoff.battery(battery);
                    published_battery_ = battery;
                }
                apply_report();
            }
            const bool rumble = s.rumble.load(std::memory_order_acquire);
            if (rumble != rumble_) {
                rumble_ = rumble;
                Bytes out{};
                (void)send(wm::build_rumble(rumble_, out), out);
            }
            if (passthrough_changed_) passthrough_changed();
            if (wanted_report() != report_) {
                apply_report();
                apply_ir();
            } else if ((s.ir.load(std::memory_order_acquire) ? wm::ir_mode_for_report(report_) : 0u) != ir_mode_) {
                apply_ir();
            }
        }
    }

    static constexpr std::uint8_t kIrUnknown = 0xff;

    wl::Link* link_ = nullptr;
    RealWiimoteConnection connection_{};
    wm::Status status_{};
    bool status_pending_ = false;
    bool gone_ = false;
    bool published_ = false;
    bool extension_ = false;
    bool rumble_ = false;
    std::uint8_t published_battery_ = 0;
    std::uint8_t report_ = 0x30;
    std::uint8_t ir_mode_ = 0;
    std::uint64_t status_asked_ms_ = 0;
    bool motion_wanted_ = false;
    std::uint8_t motion_plus_mode_ = 0;
    bool passthrough_port_ = false;
    bool passthrough_changed_ = false;
};

void name_this_thread() {
#if defined(_WIN32)
    SetThreadDescription(GetCurrentThread(), L"Nebula Wii Remote");
#elif defined(__APPLE__)
    pthread_setname_np("Nebula Wii Remote");
#elif defined(__linux__) || defined(__ANDROID__)
    pthread_setname_np(pthread_self(), "Nebula Wiimote");  // 15 characters at most
#endif
}

}  // namespace

void ensure_real_wiimote_started() noexcept {
    // A hidden start (a test) never takes the player's remote.
    if (::galaxy::launch_args().started_hidden) return;
    std::call_once(shared().started, [] {
        try {
            std::thread([] {
                name_this_thread();
                Worker worker;
                worker.run();
            }).detach();
        } catch (...) {
        }
    });
}

void real_wiimote_set_active(bool active) noexcept {
    shared().handoff.set_active(active);
}

void real_wiimote_set_report(std::uint8_t report) noexcept {
    if (shared().report.exchange(report, std::memory_order_acq_rel) != report) wl::wake();
}

void real_wiimote_set_ir(bool on) noexcept {
    if (shared().ir.exchange(on, std::memory_order_acq_rel) != on) wl::wake();
}

void real_wiimote_set_rumble(bool on) noexcept {
    if (shared().rumble.exchange(on, std::memory_order_acq_rel) != on) wl::wake();
}

WiiRemoteState wii_remote_state() noexcept {
    const std::uint8_t bits = shared().status.load(std::memory_order_acquire);
    WiiRemoteState status;
    status.connected = (bits & 1u) != 0u;
    status.nunchuk = (bits & 2u) != 0u;
    status.has_motion_plus = (bits & 4u) != 0u;
    status.motion_plus = (bits & 8u) != 0u;
    return status;
}

RealWiimoteEvent real_wiimote_wait(std::uint32_t timeout_ms, std::span<std::uint8_t, 22> report, std::size_t& size,
                                   RealWiimoteConnection& connection) noexcept {
    return shared().handoff.wait(std::chrono::milliseconds(timeout_ms), report, size, connection);
}

bool real_wiimote_in_use() noexcept {
    return shared().handoff.in_use();
}

}  // namespace galaxy::host
