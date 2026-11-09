// The real Wii Remote over Windows HID (wiimote_link.h): a remote paired with Windows, or on a DolphinBar.
#include "galaxy/wiimote_link.h"

#include "galaxy/controller_hid.h"
#include "galaxy/wiimote_hid.h"

#include "galaxy/windows_api.h"
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")

namespace galaxy::host::wiimote_link {

namespace {

namespace wm = input::wiimote;

struct Events {
    // wake(): a receive that waits for it returns.
    HANDLE control = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // A HID device arrived.
    HANDLE arrival = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HCMNOTIFICATION notification = nullptr;
};

// Never destroyed: the worker may still be blocked on the device when the process exits.
Events& events() {
    static Events* instance = new Events;
    return *instance;
}

DWORD CALLBACK on_hid_arrival(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) SetEvent(static_cast<HANDLE>(context));
    return ERROR_SUCCESS;
}

// One remote's HID handle: overlapped reads, writes at the device's output report length (hidclass takes no other).
class Remote final : public Link {
public:
    Remote() : read_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)), write_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Remote() override {
        close();
        if (read_event_ != nullptr) CloseHandle(read_event_);
        if (write_event_ != nullptr) CloseHandle(write_event_);
    }
    Remote(const Remote&) = delete;
    Remote& operator=(const Remote&) = delete;

    bool open(const std::wstring& path) {
        if (read_event_ == nullptr || write_event_ == nullptr) return false;  // no event, no overlapped I/O
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

    bool is_wii_u_pro() const {
        std::array<wchar_t, 64> name{};
        return HidD_GetProductString(handle_, name.data(), static_cast<ULONG>((name.size() - 1u) * sizeof(wchar_t))) &&
               wm::is_wii_u_pro_name(name.data());
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

    // A stack that refuses interrupt-pipe writes outright gets every write by the control pipe; a remote whose link is
    // waking answers 21 or 1167 there for a moment.
    bool send(std::span<const std::uint8_t> report) override {
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

    int receive(std::uint32_t timeout_ms, bool wakeable) override {
        if (!pending_) {
            read_ov_ = OVERLAPPED{};
            read_ov_.hEvent = read_event_;
            ResetEvent(read_event_);
            DWORD got = 0;
            if (ReadFile(handle_, in_.data(), in_length_, &got, &read_ov_)) return static_cast<int>(got);
            if (GetLastError() != ERROR_IO_PENDING) return -1;
            pending_ = true;
        }
        const HANDLE waits[2] = {read_event_, events().control};
        const DWORD count = wakeable && waits[1] != nullptr ? 2u : 1u;
        if (WaitForMultipleObjects(count, waits, FALSE, timeout_ms) != WAIT_OBJECT_0) return 0;
        pending_ = false;
        DWORD got = 0;
        if (!GetOverlappedResult(handle_, &read_ov_, &got, FALSE)) return -1;
        return static_cast<int>(got);
    }

    [[nodiscard]] std::span<const std::uint8_t> data(int size) const override {
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

}  // namespace

void start() {
    Events& e = events();
    if (e.notification != nullptr) return;
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    HidD_GetHidGuid(&filter.u.DeviceInterface.ClassGuid);
    if (e.arrival == nullptr) return;  // no event to signal: scans stay on the worker's timer
    if (CM_Register_Notification(&filter, e.arrival, on_hid_arrival, &e.notification) != CR_SUCCESS) e.notification = nullptr;
}

void each_remote(const std::function<bool(Link&)>& use) {
    const std::uint16_t products[] = {wm::kProductId, wm::kProductIdTr};
    for (const std::wstring& path : hid_interface_paths(wm::kVendorId, products)) {
        Remote remote;
        if (!remote.open(path) || remote.is_wii_u_pro()) continue;  // a Wii U Pro is a controller (controller_hid.cpp)
        if (use(remote)) return;
    }
}

bool wait_arrival(std::uint32_t timeout_ms) {
    if (events().arrival == nullptr) {  // a wait on no handle returns at once; keep the caller's pace
        Sleep(timeout_ms);
        return false;
    }
    return WaitForSingleObject(events().arrival, timeout_ms) == WAIT_OBJECT_0;
}

void wake() {
    SetEvent(events().control);
}

void set_serving(bool serving) {
    SetThreadPriority(GetCurrentThread(), serving ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_NORMAL);
}

}  // namespace galaxy::host::wiimote_link
