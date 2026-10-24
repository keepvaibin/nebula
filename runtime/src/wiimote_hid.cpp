#include "galaxy/wiimote_hid.h"

#include "galaxy/controller_hid.h"
#include "galaxy/runtime_settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")

namespace galaxy::host {

namespace {

namespace wm = input::wiimote;

struct Shared {
    RealWiimoteHandoff handoff;
    // Host to remote: what the game asks for; `control` wakes the worker.
    std::atomic<std::uint8_t> report{0x30};
    std::atomic<bool> ir{false};
    std::atomic<bool> rumble{false};
    HANDLE control = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // A HID device arrived: look for a remote again.
    HANDLE arrival = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::once_flag started;
};

// Never destroyed: the worker may still be blocked on the device when the process exits.
Shared& shared() {
    static Shared* instance = new Shared;
    return *instance;
}

bool remote_wanted() {
    const RuntimeInputMode mode = get_runtime_input_mode_state().mode;
    return mode == RuntimeInputMode::Auto || mode == RuntimeInputMode::RealWiimote;
}

DWORD CALLBACK on_hid_arrival(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) SetEvent(static_cast<HANDLE>(context));
    return ERROR_SUCCESS;
}

// One remote's HID handle: overlapped reads, writes at the device's output report length (hidclass takes no other).
class Remote {
public:
    Remote() : read_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)), write_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    bool open(const std::wstring& path) {
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        in_length_ = out_length_ = 22;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetPreparsedData(handle_, &preparsed)) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                if (caps.InputReportByteLength != 0u) in_length_ = caps.InputReportByteLength;
                if (caps.OutputReportByteLength != 0u) out_length_ = caps.OutputReportByteLength;
            }
            HidD_FreePreparsedData(preparsed);
        }
        in_.assign(in_length_, 0);
        out_.assign(out_length_, 0);
        HidD_SetNumInputBuffers(handle_, 64);
        pending_ = false;
        control_pipe_ = false;
        return true;
    }

    void close() {
        if (handle_ == INVALID_HANDLE_VALUE) return;
        if (pending_) {
            CancelIoEx(handle_, &read_ov_);
            DWORD done = 0;
            GetOverlappedResult(handle_, &read_ov_, &done, TRUE);
            pending_ = false;
        }
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

    // One output report. A stack that refuses interrupt-pipe writes outright gets every write by the control pipe; a
    // remote whose link is waking answers 21 or 1167 there for a moment. A write that only times out is not sent
    // again (it may have reached the remote): it fails, and the caller sets it up again.
    bool send(std::span<const std::uint8_t> report) {
        if (handle_ == INVALID_HANDLE_VALUE || report.size() > out_.size()) return false;
        std::fill(out_.begin(), out_.end(), std::uint8_t{0});
        std::copy(report.begin(), report.end(), out_.begin());
        if (!control_pipe_) {
            OVERLAPPED ov{};
            ov.hEvent = write_event_;
            ResetEvent(write_event_);
            DWORD done = 0;
            if (WriteFile(handle_, out_.data(), out_length_, &done, &ov)) return true;
            if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(write_event_, 100) == WAIT_OBJECT_0) {
                    return GetOverlappedResult(handle_, &ov, &done, FALSE) != FALSE;
                }
                CancelIoEx(handle_, &ov);
                return GetOverlappedResult(handle_, &ov, &done, TRUE) != FALSE;
            }
            control_pipe_ = true;
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (HidD_SetOutputReport(handle_, out_.data(), out_length_)) return true;
            const DWORD error = GetLastError();
            if (error != ERROR_NOT_READY && error != ERROR_DEVICE_NOT_CONNECTED && error != ERROR_BUSY) return false;
            Sleep(20);
        }
        return false;
    }

    // The next input report, waiting up to `timeout_ms` or until `wake` is set: its length, 0 without one, -1 when
    // the remote is gone.
    int receive(DWORD timeout_ms, HANDLE wake) {
        if (!pending_) {
            read_ov_ = OVERLAPPED{};
            read_ov_.hEvent = read_event_;
            ResetEvent(read_event_);
            DWORD got = 0;
            if (ReadFile(handle_, in_.data(), in_length_, &got, &read_ov_)) return static_cast<int>(got);
            if (GetLastError() != ERROR_IO_PENDING) return -1;
            pending_ = true;
        }
        const HANDLE waits[2] = {read_event_, wake};
        if (WaitForMultipleObjects(wake != nullptr ? 2 : 1, waits, FALSE, timeout_ms) != WAIT_OBJECT_0) return 0;
        pending_ = false;
        DWORD got = 0;
        if (!GetOverlappedResult(handle_, &read_ov_, &got, FALSE)) return -1;
        return static_cast<int>(got);
    }

    [[nodiscard]] std::span<const std::uint8_t> data(int size) const {
        return {in_.data(), static_cast<std::size_t>(size)};
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE read_event_;
    HANDLE write_event_;
    OVERLAPPED read_ov_{};
    bool pending_ = false;
    bool control_pipe_ = false;
    DWORD in_length_ = 22;
    DWORD out_length_ = 22;
    std::vector<std::uint8_t> in_;
    std::vector<std::uint8_t> out_;
};

class Worker {
public:
    void run() {
        Shared& s = shared();
        HCMNOTIFICATION notification = nullptr;
        CM_NOTIFY_FILTER filter{};
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        HidD_GetHidGuid(&filter.u.DeviceInterface.ClassGuid);
        if (CM_Register_Notification(&filter, s.arrival, on_hid_arrival, &notification) != CR_SUCCESS) notification = nullptr;
        // Looked for when the input mode allows a remote: at once, when a HID device arrives, and again after a pause
        // that grows to a minute while none answers.
        bool wanted = false;
        std::uint64_t next_scan_ms = 0;
        std::uint64_t backoff_ms = 2'000;
        for (;;) {
            const bool now_wanted = remote_wanted();
            if (now_wanted && !wanted) next_scan_ms = 0;
            wanted = now_wanted;
            const bool arrived = WaitForSingleObject(s.arrival, 0) == WAIT_OBJECT_0;
            if (wanted && (arrived || GetTickCount64() >= next_scan_ms)) {
                bool answered = false;
                const std::uint16_t products[] = {wm::kProductId, wm::kProductIdTr};
                for (const std::wstring& path : hid_interface_paths(wm::kVendorId, products)) {
                    if (!remote_.open(path)) continue;
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                    if (initialise()) {
                        answered = true;
                        serve();
                    }
                    remote_.close();
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                    if (published_) publish_disconnected();
                    if (answered) break;
                }
                if (answered) {
                    backoff_ms = 2'000;
                    next_scan_ms = 0;
                } else {
                    next_scan_ms = GetTickCount64() + backoff_ms;
                    backoff_ms = std::min<std::uint64_t>(backoff_ms * 2u, 60'000);
                }
                if (answered) continue;
            }
            // The mode is looked at again within a second; an arrival wakes it at once (and is seen above).
            if (WaitForSingleObject(s.arrival, 1000) == WAIT_OBJECT_0) SetEvent(s.arrival);
        }
    }

private:
    using Bytes = std::array<std::uint8_t, wm::kOutputBytes>;

    bool send(std::size_t used, const Bytes& out) {
        return used != 0u && remote_.send(std::span<const std::uint8_t>(out.data(), used));
    }

    void publish_connected() {
        shared().handoff.connected(connection_);
        published_ = true;
        published_battery_ = connection_.battery;
        std::cerr << "[wiimote] connected, nunchuk " << (connection_.nunchuk ? "yes" : "no") << ", battery level "
                  << static_cast<unsigned>(connection_.battery) << '\n';
    }

    void publish_disconnected() {
        shared().handoff.disconnected();
        published_ = false;
        std::cerr << "[wiimote] disconnected\n";
    }

    // One input report outside a request: data to the game, a status noted.
    void take(std::span<const std::uint8_t> report) {
        if (report.empty()) return;
        if (wm::is_data_report(report[0])) {
            shared().handoff.report(report);
        } else if (const wm::Status status = wm::parse_status(report); status.valid) {
            status_ = status;
            status_pending_ = true;
        }
    }

    // Sends `out` and reads until `match` takes a report or `timeout_ms` passes; other reports are taken.
    template <class Match>
    bool request(std::size_t used, const Bytes& out, DWORD timeout_ms, Match match) {
        if (!send(used, out)) return false;
        const std::uint64_t deadline = GetTickCount64() + timeout_ms;
        for (std::uint64_t now = GetTickCount64(); now < deadline; now = GetTickCount64()) {
            const int size = remote_.receive(static_cast<DWORD>(deadline - now), nullptr);
            if (size < 0) {
                gone_ = true;
                return false;
            }
            if (size == 0) continue;
            const std::span<const std::uint8_t> report = remote_.data(size);
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

    void apply_report() {
        Bytes out{};
        report_ = shared().report.load(std::memory_order_acquire);
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
        setup_extension(extension_);
        if (gone_) return false;
        publish_connected();
        apply_report();
        apply_ir();
        status_asked_ms_ = GetTickCount64();
        return !gone_;
    }

    void serve() {
        Shared& s = shared();
        while (!gone_ && remote_wanted()) {
            const int size = remote_.receive(1000, s.control);
            if (size < 0) return;
            if (size > 0) take(remote_.data(size));
            // The battery once a minute (the game warns when it runs low).
            if (GetTickCount64() - status_asked_ms_ >= 60'000u) {
                status_asked_ms_ = GetTickCount64();
                Bytes out{};
                (void)send(wm::build_status_request(rumble_, out), out);
            }
            if (status_pending_) {
                // Its battery; maybe an extension in or out (the remote then reports only once its mode is set again,
                // so the mode is sent after every status; the camera keeps its setup).
                status_pending_ = false;
                const std::uint8_t battery = wm::battery_level(status_.battery);
                connection_.battery = battery;
                if (status_.extension != extension_) {
                    extension_ = status_.extension;
                    setup_extension(extension_);
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
            if (s.report.load(std::memory_order_acquire) != report_) {
                apply_report();
                apply_ir();
            } else if ((s.ir.load(std::memory_order_acquire) ? wm::ir_mode_for_report(report_) : 0u) != ir_mode_) {
                apply_ir();
            }
        }
    }

    static constexpr std::uint8_t kIrUnknown = 0xff;

    Remote remote_;
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
};

}  // namespace

void ensure_real_wiimote_started() noexcept {
    // Test sessions never take the player's remote.
    if (hidden_test_session()) return;
    std::call_once(shared().started, [] {
        try {
            std::thread([] {
                SetThreadDescription(GetCurrentThread(), L"Nebula Wii Remote");
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
    Shared& s = shared();
    if (s.report.exchange(report, std::memory_order_acq_rel) != report) SetEvent(s.control);
}

void real_wiimote_set_ir(bool on) noexcept {
    Shared& s = shared();
    if (s.ir.exchange(on, std::memory_order_acq_rel) != on) SetEvent(s.control);
}

void real_wiimote_set_rumble(bool on) noexcept {
    Shared& s = shared();
    if (s.rumble.exchange(on, std::memory_order_acq_rel) != on) SetEvent(s.control);
}

RealWiimoteEvent real_wiimote_wait(std::uint32_t timeout_ms, std::span<std::uint8_t, 22> report, std::size_t& size,
                                   RealWiimoteConnection& connection) noexcept {
    return shared().handoff.wait(std::chrono::milliseconds(timeout_ms), report, size, connection);
}

bool real_wiimote_in_use() noexcept {
    return shared().handoff.in_use();
}

}  // namespace galaxy::host
