// SPDX-License-Identifier: GPL-3.0-only
// The Windows HID worker (controller_hid.cpp) with a pipe standing in for a DualSense on USB (use_hid_stand_ins). The
// pipe has no inbound buffer, so what the worker sends waits until this end reads it, as a write waits on a pad whose
// radio is busy.
//
// While it takes nothing (reports at 1 kHz for a second), the worker sends the light bar after the first report, then
// the motor once the game asks for it, and reports are read while the light bar still waits on the pad (P-083: the
// light bar was two blocking writes of up to 30 ms each on the worker's thread, so no pad's report was read while one
// waited). Once the pipe is read, the motor's on and off reports come through: a stuck request was cancelled, not left
// in the way. 2000 reports, each through the worker's completion port (P-077); the time from a report's write to
// read_hid_pads and the cycles the other threads spend a report are printed (a loaded machine stretches the time, so
// the check is the order of events, not a time).
#include "galaxy/controller_hid.h"
#include "galaxy/playstation_controller.h"
#include "galaxy/rumble.h"
#include "galaxy/windows_api.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

namespace host = galaxy::host;
namespace ps = galaxy::input::playstation;

constexpr std::uint16_t kInputBytes = 64;   // a DualSense's USB input report (id 0x01)
constexpr std::uint16_t kOutputBytes = 48;  // and its output report (id 0x02)
constexpr int kReportsEach = 1000;          // before and after the pipe is read
constexpr double kReportGapMs = 1.0;        // a USB DualSense's report rate
constexpr int kMotorFromReport = 300;       // the light bar's two requests wait up to 100 ms each before this
// Of the reports before kMotorFromReport. A worker blocked in the light bar's write reads none while it waits; an
// idle machine reads about 200 (the two requests' 100 ms each), a loaded one fewer.
constexpr int kLeastReadWhileWaiting = 1;
constexpr DWORD kComeWithinMs = 3000;

using Clock = std::chrono::steady_clock;
using Report = std::array<std::uint8_t, kInputBytes>;

int failures = 0;

void expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Owned {
    HANDLE handle = nullptr;
    explicit Owned(HANDLE h) : handle(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    ~Owned() {
        if (handle != nullptr) CloseHandle(handle);
    }
};

std::wstring pipe_name(const wchar_t* what) {
    return L"\\\\.\\pipe\\nebula-hid-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + what;
}

// The pad's end. No inbound buffer: a write from the other end waits until this end reads it.
HANDLE make_pipe(const std::wstring& name) {
    return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                            kInputBytes * 256u, 0, 0, nullptr);
}

// Bytes written to the pipe from the other end that this end has not read.
DWORD waiting_bytes(HANDLE pipe) {
    DWORD waiting = 0;
    return PeekNamedPipe(pipe, nullptr, 0, nullptr, &waiting, nullptr) ? waiting : 0u;
}

// A pipe made so takes no write until it is read, and its end sees the write waiting: the stand-in below is a pad
// that takes nothing.
void test_pipe_holds_writes() {
    const std::wstring name = pipe_name(L"probe");
    Owned server(make_pipe(name));
    expect(server.handle != nullptr, "the probe pipe is made");
    if (server.handle == nullptr) return;
    Owned client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED, nullptr));
    expect(client.handle != nullptr, "the probe pipe opens");
    if (client.handle == nullptr) return;
    Owned event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    std::array<std::uint8_t, kOutputBytes> bytes{};
    OVERLAPPED ov{};
    ov.hEvent = event.handle;
    const BOOL done = WriteFile(client.handle, bytes.data(), kOutputBytes, nullptr, &ov);
    const bool pending = !done && GetLastError() == ERROR_IO_PENDING;
    expect(pending && WaitForSingleObject(event.handle, 100) == WAIT_TIMEOUT,
           "a write to a pipe with no inbound buffer waits until the pipe is read");
    expect(waiting_bytes(server.handle) == kOutputBytes, "the pipe's end sees the write waiting");
    if (pending) {
        CancelIoEx(client.handle, &ov);
        DWORD moved = 0;
        (void)GetOverlappedResult(client.handle, &ov, &moved, TRUE);
    }
}

Report input_report(int k) {
    Report r{};
    r[0] = ps::kReportState;
    r[1] = static_cast<std::uint8_t>(16 + k % 200);          // left stick x: the report's number
    r[2] = 128;
    r[3] = static_cast<std::uint8_t>(16 + (k / 200) % 200);  // right stick x
    r[4] = 128;
    r[8] = 0x08;  // the hat at rest
    return r;
}

// The pad's end of the pipe: sends input reports, and once reading is on takes what the worker sends.
struct PadEnd {
    HANDLE pipe = nullptr;
    Owned write_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Owned read_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED read_ov{};
    std::array<std::uint8_t, kOutputBytes> chunk{};
    std::vector<std::uint8_t> bytes;
    bool reading = false;
    bool read_pending = false;

    bool send(const Report& report) {
        OVERLAPPED ov{};
        ov.hEvent = write_event.handle;
        ResetEvent(write_event.handle);
        if (!WriteFile(pipe, report.data(), kInputBytes, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        DWORD done = 0;
        return GetOverlappedResult(pipe, &ov, &done, TRUE) && done == kInputBytes;
    }

    // A write of the worker's waits here (this end not reading).
    bool write_waiting() const { return !reading && waiting_bytes(pipe) != 0u; }

    void pump() {
        while (reading) {
            DWORD done = 0;
            if (read_pending) {
                if (!HasOverlappedIoCompleted(&read_ov)) return;
                read_pending = false;
                if (!GetOverlappedResult(pipe, &read_ov, &done, FALSE)) return;
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + done);
            }
            read_ov = OVERLAPPED{};
            read_ov.hEvent = read_event.handle;
            ResetEvent(read_event.handle);
            if (ReadFile(pipe, chunk.data(), kOutputBytes, &done, &read_ov)) {
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + done);
                continue;
            }
            if (GetLastError() != ERROR_IO_PENDING) return;
            read_pending = true;
        }
    }

    // An output report taken that sets the motors, running (`on`) or stopped.
    bool has_motor_report(bool on) const {
        for (std::size_t at = 0; at + kOutputBytes <= bytes.size(); at += kOutputBytes) {
            const std::uint8_t* report = bytes.data() + at;
            if (report[0] != 0x02 || (report[1] & 0x02u) == 0u) continue;
            const bool running = report[3] != 0u || report[4] != 0u;
            if (running == on) return true;
        }
        return false;
    }
};

void ask_rumble(std::uint32_t slot, bool on) {
    host::RumbleRequest request;
    request.motor_on = on;
    request.level = on ? galaxy::input::rumble::kLevelSteps : 0u;
    request.strength = 100;
    request.target = {host::RumbleTarget::Kind::Hid, slot, galaxy::input::ControlScheme::PlayStation};
    request.focused = true;
    host::publish_rumble_request(request);
}

struct Delivery {
    double most_ms = 0.0;
    double total_ms = 0.0;
    int count = 0;
    int read_while_waiting = 0;  // reports read with a write of the worker's waiting before and after
};

// Report k from the pad's end to read_hid_pads; false when it did not come within kComeWithinMs.
bool deliver(PadEnd& pad, int k, Delivery& delivery, std::uint32_t& slot, bool watch_waits) {
    const Report report = input_report(k);
    galaxy::input::switch_hid::DecodedState want;
    ps::SensorClock clock;
    if (!ps::decode_report(ps::Model::DualSense, ps::Transport::Usb, report, ps::ImuCal{}, clock, want,
                           ps::capabilities_of_product(ps::kSonyVendorId, ps::kDualSenseProductId))) {
        return false;
    }
    std::vector<host::HidPadState> pads;
    const bool waiting_before = watch_waits && pad.write_waiting();
    const Clock::time_point start = Clock::now();
    if (!pad.send(report)) return false;
    for (;;) {
        host::read_hid_pads(pads, false);
        if (pads.size() == 1u && std::fabs(pads[0].decoded.axes[0] - want.axes[0]) < 1e-3f &&
            std::fabs(pads[0].decoded.axes[2] - want.axes[2]) < 1e-3f) {
            const double ms = ms_since(start);
            delivery.most_ms = ms > delivery.most_ms ? ms : delivery.most_ms;
            delivery.total_ms += ms;
            ++delivery.count;
            if (waiting_before && pad.write_waiting()) ++delivery.read_while_waiting;
            slot = pads[0].slot;
            return true;
        }
        if (ms_since(start) > kComeWithinMs) return false;
        pad.pump();
        SwitchToThread();
    }
}

// CPU cycles of every thread but this one: the worker's, its setup thread's and any thread-pool thread's.
std::uint64_t others_cycles() {
    ULONG64 process = 0;
    ULONG64 self = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &process);
    QueryThreadCycleTime(GetCurrentThread(), &self);
    return process - self;
}

// Reports first .. first + kReportsEach - 1 at kReportGapMs, as a pad sends them; the game asks for the motor from
// `motor_from` on (never if negative). Waits are watched before kMotorFromReport.
bool deliver_paced(PadEnd& pad, int first, Delivery& delivery, std::uint32_t& slot, int motor_from) {
    const Clock::time_point start = Clock::now();
    for (int k = first; k < first + kReportsEach; ++k) {
        while (ms_since(start) < (k - first) * kReportGapMs) {
            pad.pump();
            SwitchToThread();
        }
        if (motor_from >= 0 && k >= motor_from && k % 10 == 0) ask_rumble(slot, true);
        if (!deliver(pad, k, delivery, slot, k < kMotorFromReport)) {
            std::fprintf(stderr, "report %d did not come\n", k);
            return false;
        }
    }
    return true;
}

// Waits (taking what the worker sends, the game asking every 10 ms) for a motor report running or stopped.
bool motor_report_comes(PadEnd& pad, std::uint32_t slot, bool on) {
    const Clock::time_point start = Clock::now();
    while (ms_since(start) < kComeWithinMs) {
        ask_rumble(slot, on);
        for (int i = 0; i < 10; ++i) {
            pad.pump();
            if (pad.has_motor_report(on)) return true;
            Sleep(1);
        }
    }
    return false;
}

void report(const char* when, const Delivery& delivery, std::uint64_t cycles) {
    std::printf("%s: %d reports at 1 kHz, from write to read_hid_pads most %.2f ms, mean %.3f ms; %.0f cycles a report "
                "on the other threads\n",
                when, delivery.count, delivery.most_ms, delivery.total_ms / delivery.count,
                static_cast<double>(cycles) / delivery.count);
}

void test_worker_never_waits_on_a_write() {
    const std::wstring name = pipe_name(L"dualsense");
    Owned server(make_pipe(name));
    expect(server.handle != nullptr, "the stand-in's pipe is made");
    if (server.handle == nullptr) return;
    Owned connect_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    OVERLAPPED connect{};
    connect.hEvent = connect_event.handle;
    const BOOL now = ConnectNamedPipe(server.handle, &connect);
    const DWORD error = now ? ERROR_SUCCESS : GetLastError();
    expect(now || error == ERROR_IO_PENDING || error == ERROR_PIPE_CONNECTED, "the stand-in's pipe waits for the worker");

    host::use_hid_stand_ins({{name, ps::kSonyVendorId, ps::kDualSenseProductId, kInputBytes, kOutputBytes}});
    host::ensure_controller_hid_started();
    if (error == ERROR_IO_PENDING) {
        const bool opened = WaitForSingleObject(connect_event.handle, 10'000) == WAIT_OBJECT_0;
        expect(opened, "the worker opens the stand-in");
        if (!opened) return;
    }

    PadEnd pad;
    pad.pipe = server.handle;
    std::uint32_t slot = 0;
    // The pad takes nothing: the light bar after the first report, then the motor, wait on it until the worker
    // cancels them.
    Delivery held;
    std::uint64_t cycles = others_cycles();
    const bool held_came = deliver_paced(pad, 0, held, slot, kMotorFromReport);
    const std::uint64_t held_cycles = others_cycles() - cycles;
    expect(held_came, "every report comes while the pad takes nothing");
    if (!held_came) return;
    std::printf("reports read while the light bar waited on the pad: %d of %d\n", held.read_while_waiting,
                kMotorFromReport);
    expect(held.read_while_waiting >= kLeastReadWhileWaiting, "reports are read while the light bar waits on the pad");
    // The pad's end reads: the motor's reports come through, on and then off.
    pad.reading = true;
    expect(motor_report_comes(pad, slot, true), "a motor report comes once the pad takes writes");
    expect(motor_report_comes(pad, slot, false), "the motor's off report comes");
    Delivery taken;
    cycles = others_cycles();
    const bool taken_came = deliver_paced(pad, kReportsEach, taken, slot, -1);
    const std::uint64_t taken_cycles = others_cycles() - cycles;
    expect(taken_came, "every report comes while the pad takes writes");
    if (!taken_came) return;
    report("while the pad takes nothing", held, held_cycles);
    report("while it takes writes", taken, taken_cycles);
}

}  // namespace

int main() {
    test_pipe_holds_writes();
    if (failures == 0) test_worker_never_waits_on_a_write();
    if (failures == 0) {
        std::printf("controller_hid: every case holds\n");
    } else {
        std::printf("controller_hid: %d failed\n", failures);
    }
    // The worker never stops (the hub lives as long as the process): leave without running static destructors under it.
    std::fflush(nullptr);
    std::_Exit(failures == 0 ? 0 : 1);
}
