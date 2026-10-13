#include "galaxy/controller_hid.h"
#include "galaxy/content_viewport.h"
#include "galaxy/rumble.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <SetupAPI.h>
#include <winusb.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "winusb.lib")

namespace galaxy::host {

namespace {

namespace sw = input::switch_hid;
using input::ControlScheme;

std::uint64_t steady_us() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct HandleCloser {
    void operator()(HANDLE h) const noexcept {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

bool contains_ci(const std::wstring& text, const wchar_t* needle) {
    std::wstring a = text;
    std::wstring b = needle;
    std::transform(a.begin(), a.end(), a.begin(), ::towlower);
    std::transform(b.begin(), b.end(), b.begin(), ::towlower);
    return a.find(b) != std::wstring::npos;
}

// Synchronous helpers over an overlapped handle, used only while a device
// is being initialised (on the worker thread, before its read loop starts).
class OverlappedIo {
public:
    explicit OverlappedIo(HANDLE handle) : handle_(handle), event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    bool write(const std::uint8_t* data, DWORD size, DWORD timeout_ms) {
        OVERLAPPED ov{};
        ov.hEvent = event_.get();
        ResetEvent(ov.hEvent);
        DWORD done = 0;
        if (!WriteFile(handle_, data, size, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        return finish(ov, timeout_ms, done);
    }
    // Reads one report; false on timeout or error.
    bool read(std::uint8_t* data, DWORD size, DWORD timeout_ms, DWORD& got) {
        OVERLAPPED ov{};
        ov.hEvent = event_.get();
        ResetEvent(ov.hEvent);
        got = 0;
        if (!ReadFile(handle_, data, size, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        return finish(ov, timeout_ms, got);
    }

private:
    bool finish(OVERLAPPED& ov, DWORD timeout_ms, DWORD& done) {
        if (WaitForSingleObject(ov.hEvent, timeout_ms) != WAIT_OBJECT_0) {
            CancelIoEx(handle_, &ov);
            GetOverlappedResult(handle_, &ov, &done, TRUE);
            return false;
        }
        return GetOverlappedResult(handle_, &ov, &done, FALSE) != FALSE;
    }
    HANDLE handle_;
    UniqueHandle event_;
};

struct WinUsbPipes {
    UniqueHandle file;
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    UCHAR out_pipe = 0;
    UCHAR in_pipe = 0;
    ~WinUsbPipes() {
        if (usb != nullptr) WinUsb_Free(usb);
    }
    bool send(const sw::Switch2Command& cmd) {
        ULONG written = 0;
        return WinUsb_WritePipe(usb, out_pipe, const_cast<UCHAR*>(cmd.bytes.data()),
                                static_cast<ULONG>(cmd.size), &written, nullptr) != FALSE;
    }
    // Reads up to `size` bytes in 64-byte packets until a short packet.
    std::size_t receive(std::uint8_t* data, std::size_t size) {
        std::size_t total = 0;
        while (total < size) {
            ULONG got = 0;
            const ULONG chunk = static_cast<ULONG>(std::min<std::size_t>(64u, size - total));
            if (!WinUsb_ReadPipe(usb, in_pipe, data + total, chunk, &got, nullptr)) break;
            total += got;
            if (got < chunk) break;
        }
        return total;
    }
};

struct Device {
    std::wstring path;
    ControlScheme scheme = ControlScheme::SwitchPro;
    bool bluetooth = false;
    UniqueHandle handle;
    UniqueHandle read_event;
    OVERLAPPED read_ov{};
    bool read_pending = false;
    bool resume = false;   // read_event set by hand to continue a burst
    bool failed = false;   // read error; the next rescan reopens it
    std::vector<std::uint8_t> buffer;
    USHORT output_length = 64;
    sw::StickCal left{};
    sw::StickCal right{};
    sw::ImuCal imu{};
    sw::Switch2ImuState switch2_imu{};
    sw::SwitchMotion motion{};
    std::uint64_t last_report_us = 0;
    std::uint64_t imu_reports = 0;
    std::unique_ptr<WinUsbPipes> winusb;
    std::size_t slot = 0;
    // Output packet number after initialisation (0x10 rumble reports).
    std::uint8_t packet = 0;
    // Vibration last sent to this controller.
    bool rumble_on = false;
    std::uint8_t rumble_strength = 0;
    std::uint8_t rumble_sequence = 0;
    std::uint64_t rumble_sent_us = 0;
    UniqueHandle write_event;
};

struct Published {
    bool connected = false;
    SwitchPadState state{};
};

struct Hub {
    std::mutex lifecycle;
    std::thread worker;
    UniqueHandle stop_event;
    UniqueHandle rescan_event;
    HCMNOTIFICATION notification = nullptr;
    SRWLOCK lock = SRWLOCK_INIT;
    std::array<Published, kMaxSwitchPads> pads{};
    std::atomic<bool> started{false};
    std::atomic<unsigned> pro_count{0};
    std::atomic<unsigned> pro2_count{0};
    std::atomic<bool> pro2_needs_winusb{false};
    // Vibration: the game state from the emulation thread, and what runs on
    // the devices (read without locks by stop_all_rumble).
    SRWLOCK rumble_lock = SRWLOCK_INIT;
    RumbleRequest rumble_request{};
    std::uint64_t rumble_published_us = 0;
    UniqueHandle rumble_event;
    // Set by stop_all_rumble (exit, crash): no motor starts again.
    std::atomic<bool> rumble_shutdown{false};
    std::atomic<std::uint8_t> xinput_rumbling{0};  // bit per XInput user index
    // A Switch controller's handle while its motor may run; cleared under
    // rumble_device_lock (exclusive) before the handle closes.
    SRWLOCK rumble_device_lock = SRWLOCK_INIT;
    std::array<std::atomic<HANDLE>, kMaxSwitchPads> switch_rumble_handles{};
    std::array<std::atomic<std::uint16_t>, kMaxSwitchPads> switch_rumble_lengths{};
    std::array<std::atomic<bool>, kMaxSwitchPads> switch_rumble_pro2{};
};

// Never destroyed: the worker may still be blocked when the process exits.
Hub& hub() {
    static Hub* instance = new Hub;
    return *instance;
}

DWORD CALLBACK on_device_change(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action,
                                PCM_NOTIFY_EVENT_DATA, DWORD) {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL ||
        action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
        SetEvent(static_cast<HANDLE>(context));
    }
    return ERROR_SUCCESS;
}

struct HidCandidate {
    std::wstring path;
    USHORT product = 0;
};

std::vector<HidCandidate> enumerate_switch_hid() {
    std::vector<HidCandidate> found;
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);
    const HDEVINFO set = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return found;
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid_guid, index, &iface); ++index) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;
        std::vector<std::uint8_t> storage(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, nullptr)) continue;
        const std::wstring path = detail->DevicePath;
        if (!contains_ci(path, L"057e")) continue;  // cheap filter before opening
        UniqueHandle probe(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, 0, nullptr));
        if (probe.get() == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(probe.get(), &attributes) || attributes.VendorID != sw::kNintendoVendorId) continue;
        if (attributes.ProductID != sw::kProControllerProductId &&
            attributes.ProductID != sw::kPro2ControllerProductId) {
            continue;
        }
        found.push_back({path, attributes.ProductID});
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

// The Pro Controller 2's vendor interface (USB interface 1) bound to WinUSB:
// read the interface GUID WinUSB registered for that device instance.
std::unique_ptr<WinUsbPipes> open_switch2_vendor_interface() {
    const HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return nullptr;
    std::unique_ptr<WinUsbPipes> result;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD index = 0; !result && SetupDiEnumDeviceInfo(set, index, &info); ++index) {
        wchar_t instance[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(info.DevInst, instance, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) continue;
        const std::wstring id = instance;
        if (!contains_ci(id, L"VID_057E&PID_2069&MI_01")) continue;
        HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t guids[512]{};
        DWORD bytes = sizeof(guids) - sizeof(wchar_t);
        DWORD type = 0;
        LSTATUS status = RegQueryValueExW(key, L"DeviceInterfaceGUIDs", nullptr, &type,
                                          reinterpret_cast<LPBYTE>(guids), &bytes);
        if (status != ERROR_SUCCESS) {
            bytes = sizeof(guids) - sizeof(wchar_t);
            status = RegQueryValueExW(key, L"DeviceInterfaceGUID", nullptr, &type,
                                      reinterpret_cast<LPBYTE>(guids), &bytes);
        }
        RegCloseKey(key);
        if (status != ERROR_SUCCESS) continue;
        GUID guid{};
        if (CLSIDFromString(guids, &guid) != NOERROR) continue;
        ULONG length = 0;
        if (CM_Get_Device_Interface_List_SizeW(&length, &guid, instance, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
                CR_SUCCESS ||
            length <= 1) {
            continue;
        }
        std::vector<wchar_t> list(length);
        if (CM_Get_Device_Interface_ListW(&guid, instance, list.data(), length,
                                          CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) {
            continue;
        }
        auto pipes = std::make_unique<WinUsbPipes>();
        pipes->file.reset(CreateFileW(list.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr));
        if (pipes->file.get() == INVALID_HANDLE_VALUE) continue;
        if (!WinUsb_Initialize(pipes->file.get(), &pipes->usb)) {
            pipes->usb = nullptr;
            continue;
        }
        USB_INTERFACE_DESCRIPTOR descriptor{};
        if (WinUsb_QueryInterfaceSettings(pipes->usb, 0, &descriptor)) {
            for (UCHAR p = 0; p < descriptor.bNumEndpoints; ++p) {
                WINUSB_PIPE_INFORMATION pipe{};
                if (!WinUsb_QueryPipe(pipes->usb, 0, p, &pipe) || pipe.PipeType != UsbdPipeTypeBulk) continue;
                if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) pipes->in_pipe = pipe.PipeId;
                else pipes->out_pipe = pipe.PipeId;
            }
        }
        if (pipes->in_pipe == 0 || pipes->out_pipe == 0) continue;
        ULONG timeout = 500;
        WinUsb_SetPipePolicy(pipes->usb, pipes->in_pipe, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
        WinUsb_SetPipePolicy(pipes->usb, pipes->out_pipe, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
        result = std::move(pipes);
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

bool flash_read_switch2(WinUsbPipes& pipes, std::uint32_t address, std::array<std::uint8_t, 0x40>& out) {
    if (!pipes.send(sw::switch2_flash_read(address))) return false;
    std::array<std::uint8_t, 0x50> reply{};
    if (pipes.receive(reply.data(), reply.size()) < 0x50) return false;
    std::memcpy(out.data(), reply.data() + 0x10, out.size());
    return true;
}

void init_switch2(Device& device) {
    device.winusb = open_switch2_vendor_interface();
    if (!device.winusb) {
        hub().pro2_needs_winusb.store(true, std::memory_order_release);
        std::cerr << "[controller] pro controller 2 (usb): interface 1 has no WinUSB driver; "
                     "input needs the vendor init command (docs/KEYBOARD_CONTROLS.md)\n";
        return;
    }
    WinUsbPipes& pipes = *device.winusb;
    std::array<std::uint8_t, 0x40> block{};
    if (flash_read_switch2(pipes, 0x13040, block)) {
        float bias[3]{};
        std::memcpy(bias, block.data() + 4, sizeof(bias));
        for (int i = 0; i < 3; ++i) {
            device.switch2_imu.gyro_bias[static_cast<std::size_t>(i)] = std::isfinite(bias[i]) ? bias[i] : 0.0f;
        }
    }
    if (flash_read_switch2(pipes, 0x13080, block)) {
        device.left = sw::parse_switch2_stick_cal(std::span<const std::uint8_t, 9>(block.data() + 0x28, 9));
    }
    if (flash_read_switch2(pipes, 0x130c0, block)) {
        device.right = sw::parse_switch2_stick_cal(std::span<const std::uint8_t, 9>(block.data() + 0x28, 9));
    }
    if (flash_read_switch2(pipes, 0x1fc040, block) && block[0] == 0xb2 && block[1] == 0xa1) {
        device.left = sw::parse_switch2_stick_cal(std::span<const std::uint8_t, 9>(block.data() + 2, 9));
    }
    if (flash_read_switch2(pipes, 0x1fc080, block) && block[0] == 0xb2 && block[1] == 0xa1) {
        device.right = sw::parse_switch2_stick_cal(std::span<const std::uint8_t, 9>(block.data() + 2, 9));
    }
    std::array<std::uint8_t, 64> reply{};
    for (const sw::Switch2Command& cmd : sw::switch2_init_sequence()) {
        if (!pipes.send(cmd)) {
            std::cerr << "[controller] pro controller 2: init command failed\n";
            return;
        }
        (void)pipes.receive(reply.data(), reply.size());
    }
    if (pipes.send(sw::switch2_player_led(0x01))) (void)pipes.receive(reply.data(), 8);
}

// Sends one subcommand and waits for its acknowledged 0x21 reply.
bool subcommand(Device& device, OverlappedIo& io, std::uint8_t& packet, std::uint8_t id,
                std::span<const std::uint8_t> data, std::uint32_t spi_address,
                std::vector<std::uint8_t>* reply_data) {
    std::vector<std::uint8_t> out(std::max<std::size_t>(device.output_length, 64u));
    std::vector<std::uint8_t> in(std::max<std::size_t>(device.buffer.size(), 64u));
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::size_t size = 0;
        if (id == sw::kSubcommandSpiRead) {
            size = sw::build_spi_read(packet++, spi_address, data.empty() ? 0 : data[0], out);
        } else {
            size = sw::build_subcommand(packet++, id, data, out);
        }
        if (size == 0 || !io.write(out.data(), device.output_length, 200)) continue;
        const std::uint64_t deadline = GetTickCount64() + 150u;
        while (GetTickCount64() < deadline) {
            DWORD got = 0;
            if (!io.read(in.data(), static_cast<DWORD>(in.size()), 60, got)) continue;
            const auto reply = sw::match_subcommand_reply(std::span<const std::uint8_t>(in.data(), got), id, spi_address);
            if (reply.matched) {
                if (reply_data != nullptr) reply_data->assign(reply.data.begin(), reply.data.end());
                return true;
            }
        }
    }
    return false;
}

bool usb_command(Device& device, OverlappedIo& io, std::uint8_t command, bool wait) {
    std::vector<std::uint8_t> out(std::max<std::size_t>(device.output_length, 64u));
    std::vector<std::uint8_t> in(std::max<std::size_t>(device.buffer.size(), 64u));
    (void)sw::build_usb_command(command, out);
    if (!io.write(out.data(), device.output_length, 200)) return false;
    if (!wait) return true;
    const std::uint64_t deadline = GetTickCount64() + 150u;
    while (GetTickCount64() < deadline) {
        DWORD got = 0;
        if (io.read(in.data(), static_cast<DWORD>(in.size()), 60, got) && got >= 2u &&
            in[0] == sw::kReportUsbAck && in[1] == command) {
            return true;
        }
    }
    return false;
}

void init_switch_pro(Device& device) {
    OverlappedIo io(device.handle.get());
    std::uint8_t packet = 0;
    if (!device.bluetooth) {
        if (usb_command(device, io, sw::kUsbHandshake, true)) {
            (void)usb_command(device, io, sw::kUsbHighSpeed, true);
            (void)usb_command(device, io, sw::kUsbHandshake, true);
            (void)usb_command(device, io, sw::kUsbForceHid, false);
        } else {
            device.bluetooth = true;  // no USB acknowledgement
        }
    }
    std::vector<std::uint8_t> data;
    bool user_left = false;
    bool user_right = false;
    const std::uint8_t user_length = 22;
    if (subcommand(device, io, packet, sw::kSubcommandSpiRead, std::span(&user_length, 1), sw::kSpiUserStickCal, &data) &&
        data.size() >= 22u) {
        if (data[0] == 0xb2 && data[1] == 0xa1) {
            device.left = sw::parse_left_stick_cal(std::span<const std::uint8_t, 9>(data.data() + 2, 9));
            user_left = true;
        }
        if (data[11] == 0xb2 && data[12] == 0xa1) {
            device.right = sw::parse_right_stick_cal(std::span<const std::uint8_t, 9>(data.data() + 13, 9));
            user_right = true;
        }
    }
    const std::uint8_t factory_length = 18;
    if ((!user_left || !user_right) &&
        subcommand(device, io, packet, sw::kSubcommandSpiRead, std::span(&factory_length, 1), sw::kSpiFactoryStickCal,
                   &data) &&
        data.size() >= 18u) {
        if (!user_left) device.left = sw::parse_left_stick_cal(std::span<const std::uint8_t, 9>(data.data(), 9));
        if (!user_right) device.right = sw::parse_right_stick_cal(std::span<const std::uint8_t, 9>(data.data() + 9, 9));
    }
    const std::uint8_t imu_length = 24;
    if (subcommand(device, io, packet, sw::kSubcommandSpiRead, std::span(&imu_length, 1), sw::kSpiFactoryImuCal, &data) &&
        data.size() >= 24u) {
        device.imu = sw::parse_imu_cal(std::span<const std::uint8_t, 24>(data.data(), 24));
    }
    const std::uint8_t user_imu_length = 26;
    if (subcommand(device, io, packet, sw::kSubcommandSpiRead, std::span(&user_imu_length, 1), sw::kSpiUserImuCal,
                   &data) &&
        data.size() >= 26u && data[0] == 0xb2 && data[1] == 0xa1) {
        device.imu = sw::parse_imu_cal(std::span<const std::uint8_t, 24>(data.data() + 2, 24));
    }
    const std::uint8_t full_mode = sw::kReportFullState;
    const std::uint8_t on = 1;
    const std::uint8_t player_one = 0x01;
    // Vibration is enabled so the game's motor can be mirrored; the motor
    // stays silent until the game turns it on (worker service_rumble).
    const std::uint8_t vibration = rumble_output_disabled() ? 0 : 1;
    const bool mode_ok = subcommand(device, io, packet, sw::kSubcommandSetReportMode, std::span(&full_mode, 1), 0, nullptr);
    const bool imu_ok = subcommand(device, io, packet, sw::kSubcommandEnableImu, std::span(&on, 1), 0, nullptr);
    (void)subcommand(device, io, packet, sw::kSubcommandEnableVibration, std::span(&vibration, 1), 0, nullptr);
    (void)subcommand(device, io, packet, sw::kSubcommandPlayerLights, std::span(&player_one, 1), 0, nullptr);
    device.packet = packet;
    std::cerr << "[controller] switch pro (" << (device.bluetooth ? "bluetooth" : "usb")
              << ") report 0x30 " << (mode_ok ? "on" : "unconfirmed") << ", imu "
              << (imu_ok ? "on" : "unconfirmed") << '\n';
}

// One output report on an overlapped device handle, waiting at most
// `timeout_ms`; a stuck write is cancelled.
bool write_report(HANDLE handle, HANDLE event, const std::uint8_t* data, DWORD size, DWORD timeout_ms) noexcept {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || event == nullptr) return false;
    OVERLAPPED ov{};
    ov.hEvent = event;
    ResetEvent(event);
    DWORD done = 0;
    if (!WriteFile(handle, data, size, nullptr, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return false;
        if (WaitForSingleObject(event, timeout_ms) != WAIT_OBJECT_0) {
            CancelIoEx(handle, &ov);
            GetOverlappedResult(handle, &ov, &done, TRUE);
            return false;
        }
    }
    return GetOverlappedResult(handle, &ov, &done, FALSE) != FALSE;
}

// The motor-off report for a Switch controller.
bool write_rumble_off(HANDLE handle, HANDLE event, std::uint16_t output_length, bool pro2) noexcept {
    std::array<std::uint8_t, 128> report{};
    const std::size_t length = std::min<std::size_t>(output_length, report.size());
    const std::size_t built = pro2
        ? input::rumble::build_switch2_rumble_report(0, false, 0, std::span(report.data(), length))
        : input::rumble::build_switch_pro_rumble_report(0, input::rumble::switch_pro_motor_rumble(false, 0),
                                                        std::span(report.data(), length));
    return built != 0 && write_report(handle, event, report.data(), static_cast<DWORD>(length), 30);
}

// Set unless the value is empty, 0, false, off or no.
bool env_flag(const char* name) noexcept {
    char value[16]{};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    if (length == 0) return false;
    if (length >= sizeof(value)) return true;
    for (DWORD i = 0; i < length; ++i) {
        value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }
    const std::string_view text(value, length);
    return !(text == "0" || text == "false" || text == "off" || text == "no");
}

bool rumble_trace_enabled() noexcept {
    static const bool enabled = env_flag("GALAXY_RUMBLE_TRACE");
    return enabled;
}

// GALAXY_TEST_RUMBLE_TARGET=xinput:N or switch:N, honoured only while
// output is disabled: the controller a trace session reports as vibrating,
// with the window counted as focused (a hidden window never is).
RumbleTarget test_rumble_target() noexcept {
    static const RumbleTarget target = [] {
        RumbleTarget parsed{};
        char value[32]{};
        const DWORD length = GetEnvironmentVariableA("GALAXY_TEST_RUMBLE_TARGET", value, sizeof(value));
        if (length == 0 || length >= sizeof(value) || !rumble_output_disabled()) return parsed;
        const std::string_view text(value, length);
        const std::size_t colon = text.find(':');
        if (colon == std::string_view::npos || colon + 2u != text.size()) return parsed;
        const char digit = text[colon + 1u];
        if (digit < '0' || digit > '3') return parsed;
        const std::string_view kind = text.substr(0, colon);
        if (kind == "xinput") parsed.kind = RumbleTarget::Kind::XInput;
        else if (kind == "switch") parsed.kind = RumbleTarget::Kind::Switch;
        parsed.slot = static_cast<std::uint8_t>(digit - '0');
        return parsed;
    }();
    return target;
}

const char* rumble_target_kind_name(RumbleTarget::Kind kind) noexcept {
    switch (kind) {
    case RumbleTarget::Kind::XInput: return "xinput";
    case RumbleTarget::Kind::Switch: return "switch";
    default: return "none";
    }
}

// GALAXY_RUMBLE_TRACE: what the controller in use would receive, logged at
// every change of the gated motor state (nothing is sent to any device).
void trace_rumble_output(const RumbleRequest& request, std::uint64_t now) noexcept {
    static bool s_logged = false;
    static bool s_run = false;
    static RumbleTarget s_target{};
    static std::uint8_t s_strength = 0;
    static unsigned s_rows = 0;
    input::rumble::GateInputs gate{};
    gate.game_motor_on = request.motor_on;
    gate.now_us = now;
    gate.published_us = now;
    gate.focused = request.focused;
    gate.paused = request.paused;
    gate.strength = request.strength;
    const bool has_target = request.target.kind != RumbleTarget::Kind::None;
    const bool run = has_target && input::rumble::motor_should_run(gate);
    if (s_logged && run == s_run && request.target == s_target && request.strength == s_strength) return;
    if (s_rows >= 4000u) return;
    ++s_rows;
    s_logged = true;
    s_run = run;
    s_target = request.target;
    s_strength = request.strength;
    const char* reason = run ? "" : !request.motor_on ? " game-off" : !has_target ? " no-controller"
                                  : request.strength == 0u ? " strength-off" : !request.focused ? " unfocused"
                                  : request.paused ? " paused" : " gate";
    char line[256]{};
    int used = std::snprintf(line, sizeof(line), "[rumble-out] us=%llu target=%s:%u motor=%s%s strength=%u",
                             static_cast<unsigned long long>(now), rumble_target_kind_name(request.target.kind),
                             static_cast<unsigned>(request.target.slot), run ? "on" : "off", reason,
                             static_cast<unsigned>(request.strength));
    if (used > 0 && static_cast<std::size_t>(used) < sizeof(line)) {
        if (request.target.kind == RumbleTarget::Kind::XInput) {
            const input::rumble::XInputMotors speeds = input::rumble::xinput_motor_speeds(run, request.strength);
            used += std::snprintf(line + used, sizeof(line) - static_cast<std::size_t>(used), " xinput=%u/%u",
                                  static_cast<unsigned>(speeds.left), static_cast<unsigned>(speeds.right));
        } else if (request.target.kind == RumbleTarget::Kind::Switch) {
            const std::array<std::uint8_t, 8> pro = input::rumble::switch_pro_motor_rumble(run, request.strength);
            std::array<std::uint8_t, input::rumble::kSwitch2RumbleReportBytes> pro2{};
            (void)input::rumble::build_switch2_rumble_report(0, run, request.strength, pro2);
            used += std::snprintf(line + used, sizeof(line) - static_cast<std::size_t>(used),
                                  " pro=%02x%02x%02x%02x.%02x%02x%02x%02x pro2=%02x%02x%02x%02x%02x", pro[0],
                                  pro[1], pro[2], pro[3], pro[4], pro[5], pro[6], pro[7], pro2[2], pro2[3], pro2[4],
                                  pro2[5], pro2[6]);
        }
    }
    std::cerr << line << '\n';
}

// Switch output pacing: SDL's hidapi drivers space Pro Controller writes at
// least ~30 ms apart (faster writes can drop a Bluetooth link) and repeat a
// held rumble so it does not fade; the Pro Controller 2 takes a fresh motor
// packet every 12 ms.
constexpr std::uint64_t kSwitchRumbleGapUs = 30'000;
constexpr std::uint64_t kSwitchRumbleRefreshUs = 40'000;
constexpr std::uint64_t kSwitch2RumbleRefreshUs = 12'000;

class Worker {
public:
    void run(HANDLE stop, HANDLE rescan, HANDLE rumble) {
        rescan_devices();
        for (;;) {
            std::array<HANDLE, 3 + kMaxSwitchPads> waits{};
            std::array<Device*, 3 + kMaxSwitchPads> owners{};
            DWORD count = 0;
            waits[count++] = stop;
            waits[count++] = rescan;
            waits[count++] = rumble;
            for (auto& device : devices_) {
                if (device->read_pending) {
                    owners[count] = device.get();
                    waits[count++] = device->read_event.get();
                }
            }
            // A timer only while a motor runs: it refreshes the motor and
            // stops it when the game's state goes stale.
            const DWORD result = WaitForMultipleObjects(count, waits.data(), FALSE,
                                                        rumble_timer_ms_ != 0u ? rumble_timer_ms_ : INFINITE);
            if (result == WAIT_OBJECT_0) break;
            if (result == WAIT_OBJECT_0 + 1) {
                rescan_devices();
            } else if (result >= WAIT_OBJECT_0 + 3 && result < WAIT_OBJECT_0 + count) {
                Device* device = owners[result - WAIT_OBJECT_0];
                complete_read(*device);
            } else if (result != WAIT_OBJECT_0 + 2 && result != WAIT_TIMEOUT) {
                break;  // WAIT_FAILED: leave rather than spin
            }
            service_rumble();
        }
        stop_xinput_rumble();
        for (auto& device : devices_) close_device(*device);
        devices_.clear();
    }

private:
    void refresh_config() {
        const std::uint64_t generation = input::controls_config_generation();
        if (generation != config_generation_) {
            config_ = input::get_controls_config();
            config_generation_ = generation;
        }
    }

    void rescan_devices() {
        const auto candidates = enumerate_switch_hid();
        // Drop devices that disappeared.
        for (auto it = devices_.begin(); it != devices_.end();) {
            const bool present = std::any_of(candidates.begin(), candidates.end(),
                                             [&](const HidCandidate& c) { return c.path == (*it)->path; });
            if (present && !(*it)->failed) {
                ++it;
            } else {
                close_device(**it);
                it = devices_.erase(it);
            }
        }
        for (const HidCandidate& candidate : candidates) {
            if (devices_.size() >= kMaxSwitchPads) break;
            const bool known = std::any_of(devices_.begin(), devices_.end(),
                                           [&](const auto& d) { return d->path == candidate.path; });
            if (!known) open_device(candidate);
        }
        unsigned pro = 0, pro2 = 0;
        for (const auto& device : devices_) {
            (device->scheme == ControlScheme::SwitchPro2 ? pro2 : pro)++;
        }
        hub().pro_count.store(pro, std::memory_order_release);
        hub().pro2_count.store(pro2, std::memory_order_release);
    }

    std::size_t free_slot() const {
        for (std::size_t slot = 0; slot < kMaxSwitchPads; ++slot) {
            const bool used = std::any_of(devices_.begin(), devices_.end(),
                                          [slot](const auto& d) { return d->slot == slot; });
            if (!used) return slot;
        }
        return kMaxSwitchPads;
    }

    void open_device(const HidCandidate& candidate) {
        auto device = std::make_unique<Device>();
        device->path = candidate.path;
        device->scheme = candidate.product == sw::kPro2ControllerProductId ? ControlScheme::SwitchPro2
                                                                            : ControlScheme::SwitchPro;
        device->slot = free_slot();
        if (device->slot >= kMaxSwitchPads) return;
        device->handle.reset(CreateFileW(candidate.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OVERLAPPED, nullptr));
        if (device->handle.get() == INVALID_HANDLE_VALUE) return;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetPreparsedData(device->handle.get(), &preparsed)) {
            (void)HidP_GetCaps(preparsed, &caps);
            HidD_FreePreparsedData(preparsed);
        }
        device->buffer.resize(std::max<USHORT>(caps.InputReportByteLength, 64));
        device->output_length = std::max<USHORT>(caps.OutputReportByteLength, 2);
        device->bluetooth = contains_ci(candidate.path, L"{00001124-0000-1000-8000-00805f9b34fb}");
        device->read_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (device->scheme == ControlScheme::SwitchPro) {
            init_switch_pro(*device);
        } else if (device->bluetooth) {
            std::cerr << "[controller] pro controller 2 over bluetooth is not a HID device on Windows; "
                         "connect it by USB\n";
        } else {
            init_switch2(*device);
        }
        {
            AcquireSRWLockExclusive(&hub().lock);
            Published& published = hub().pads[device->slot];
            published = {};
            published.connected = true;
            published.state.connected = true;
            published.state.scheme = device->scheme;
            published.state.left = device->left;
            published.state.right = device->right;
            published.state.slot = static_cast<std::uint8_t>(device->slot);
            ReleaseSRWLockExclusive(&hub().lock);
        }
        std::cerr << "[controller] connected " << input::control_scheme_id(device->scheme)
                  << (device->bluetooth ? " bluetooth" : " usb") << '\n';
        Device& ref = *device;
        devices_.push_back(std::move(device));
        issue_read(ref);
    }

    void close_device(Device& device) {
        if (device.slot < kMaxSwitchPads) {
            AcquireSRWLockExclusive(&hub().rumble_device_lock);
            hub().switch_rumble_handles[device.slot].store(nullptr);
            ReleaseSRWLockExclusive(&hub().rumble_device_lock);
        }
        if (device.rumble_on) {
            if (!device.write_event) device.write_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            (void)write_rumble_off(device.handle.get(), device.write_event.get(), device.output_length,
                                   device.scheme == ControlScheme::SwitchPro2);
            device.rumble_on = false;
        }
        if (device.read_pending && !device.resume) {
            CancelIoEx(device.handle.get(), &device.read_ov);
            DWORD done = 0;
            GetOverlappedResult(device.handle.get(), &device.read_ov, &done, TRUE);
            device.read_pending = false;
        }
        if (device.slot < kMaxSwitchPads) {
            AcquireSRWLockExclusive(&hub().lock);
            hub().pads[device.slot] = {};
            ReleaseSRWLockExclusive(&hub().lock);
        }
        std::cerr << "[controller] disconnected " << input::control_scheme_id(device.scheme) << '\n';
    }

    void issue_read(Device& device) {
        // A synchronous completion is processed inline; bound the loop so
        // a device flooding reports cannot starve the other waits.
        for (int burst = 0; burst < 8; ++burst) {
            device.read_ov = OVERLAPPED{};
            device.read_ov.hEvent = device.read_event.get();
            ResetEvent(device.read_event.get());
            DWORD got = 0;
            if (ReadFile(device.handle.get(), device.buffer.data(), static_cast<DWORD>(device.buffer.size()), &got,
                         &device.read_ov)) {
                handle_report(device, got);
                continue;
            }
            if (GetLastError() == ERROR_IO_PENDING) {
                device.read_pending = true;
                return;
            }
            device.read_pending = false;
            device.failed = true;
            SetEvent(rescan_handle_);
            return;
        }
        device.resume = true;
        device.read_pending = true;
        SetEvent(device.read_event.get());  // continue after the other waits
    }

    void complete_read(Device& device) {
        device.read_pending = false;
        if (device.resume) {
            device.resume = false;
            issue_read(device);
            return;
        }
        DWORD got = 0;
        if (!GetOverlappedResult(device.handle.get(), &device.read_ov, &got, FALSE)) {
            // Unplugged or the stack reset: reopen on the rescan.
            device.failed = true;
            SetEvent(rescan_handle_);
            return;
        }
        handle_report(device, got);
        issue_read(device);
    }

    void handle_report(Device& device, DWORD size) {
        const std::span<const std::uint8_t> report(device.buffer.data(), size);
        sw::DecodedState decoded{};
        const bool ok = device.scheme == ControlScheme::SwitchPro2
                            ? sw::decode_switch2_report(report, device.switch2_imu, decoded)
                            : sw::decode_pro_report(report, device.imu, decoded);
        if (!ok) return;
        refresh_config();
        const input::SchemeTuning& tuning = config_.tune(device.scheme);
        const std::uint64_t now = steady_us();
        float dt_report = device.last_report_us != 0
                              ? static_cast<float>(now - device.last_report_us) * 1e-6f : 0.0f;
        dt_report = std::clamp(dt_report, 0.0f, 0.05f);
        device.last_report_us = now;
        float gyro_x = 0.0f, gyro_y = 0.0f;
        bool shake_started = false;
        sw::MotionSettings motion{};
        motion.shake = tuning.motion_shake != input::MotionShake::Off;
        motion.shake_sensitivity = tuning.shake_sensitivity;
        motion.gyro_pointer = tuning.gyro_pointer;
        motion.gyro_sensitivity = tuning.gyro_sensitivity;
        motion.picture_aspect = static_cast<float>(galaxy::kWiiContentAspect);
        for (std::size_t i = 0; i < decoded.imu_count; ++i) {
            // Switch Pro: three samples 5 ms apart; Pro 2: one per report.
            const float dt = device.scheme == ControlScheme::SwitchPro ? 0.005f : dt_report;
            const std::uint64_t t = now - static_cast<std::uint64_t>((decoded.imu_count - 1u - i) * 5000u);
            const sw::SwitchMotion::Output out = device.motion.feed(decoded.imu[i], dt, t, motion);
            shake_started |= out.shake_started;
            gyro_x += out.pointer_x;
            gyro_y += out.pointer_y;
        }
        if (decoded.imu_count != 0u) {
            ++device.imu_reports;
        }
        AcquireSRWLockExclusive(&hub().lock);
        Published& published = hub().pads[device.slot];
        published.connected = true;
        SwitchPadState& state = published.state;
        state.connected = true;
        state.scheme = device.scheme;
        state.decoded = decoded;
        state.left = device.left;
        state.right = device.right;
        if (shake_started) state.shake_until_us = now + sw::SwitchMotion::kShakeHoldUs;
        state.gyro_x = std::clamp(state.gyro_x + gyro_x, -4.0f, 4.0f);
        state.gyro_y = std::clamp(state.gyro_y + gyro_y, -4.0f, 4.0f);
        state.imu_live = decoded.imu_count != 0u;
        state.gyro_rest_windows = device.motion.rest_windows();
        state.gravity = device.motion.gravity();
        state.gravity_valid = device.motion.gravity_ready();
        state.slot = static_cast<std::uint8_t>(device.slot);
        ReleaseSRWLockExclusive(&hub().lock);
    }

    // ---- Vibration ------------------------------------------------------------
    // Applies the game's published motor state (rumble::motor_should_run) to
    // the controller in use and turns every other motor off. Runs after every
    // wake: a device report, a publication, a rescan or the motor timer.
    void service_rumble() {
        Hub& h = hub();
        AcquireSRWLockShared(&h.rumble_lock);
        const RumbleRequest request = h.rumble_request;
        const std::uint64_t published = h.rumble_published_us;
        ReleaseSRWLockShared(&h.rumble_lock);
        const std::uint64_t now = steady_us();
        input::rumble::GateInputs gate{};
        gate.game_motor_on = request.motor_on;
        gate.now_us = now;
        gate.published_us = published;
        gate.focused = request.focused;
        gate.paused = request.paused;
        gate.strength = request.strength;
        const bool run = !rumble_output_disabled() && !h.rumble_shutdown.load(std::memory_order_acquire) &&
                         input::rumble::motor_should_run(gate);
        // XInput motors hold their speed: one call per change. A call that
        // fails (no controller in that slot) is not repeated until the
        // wanted state changes, since XInput calls on empty slots are slow.
        const bool xinput = run && request.target.kind == RumbleTarget::Kind::XInput &&
                            request.target.slot < XUSER_MAX_COUNT;
        const std::uint8_t want_mask = xinput ? static_cast<std::uint8_t>(1u << request.target.slot) : 0u;
        const std::uint8_t want_strength = xinput ? request.strength : 0u;
        if (want_mask != xinput_want_mask_ || want_strength != xinput_want_strength_) {
            xinput_want_mask_ = want_mask;
            xinput_want_strength_ = want_strength;
            for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
                const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
                if ((want_mask & bit) != 0u) {
                    const input::rumble::XInputMotors speeds = input::rumble::xinput_motor_speeds(true, want_strength);
                    XINPUT_VIBRATION vibration{};
                    vibration.wLeftMotorSpeed = speeds.left;
                    vibration.wRightMotorSpeed = speeds.right;
                    // Marked before the call: stop_all_rumble may run at any time.
                    h.xinput_rumbling.fetch_or(bit, std::memory_order_acq_rel);
                    if (XInputSetState(slot, &vibration) == ERROR_SUCCESS) {
                        xinput_on_ |= bit;
                    } else {
                        h.xinput_rumbling.fetch_and(static_cast<std::uint8_t>(~bit), std::memory_order_acq_rel);
                    }
                } else if ((xinput_on_ & bit) != 0u) {
                    XINPUT_VIBRATION off{};
                    (void)XInputSetState(slot, &off);
                    xinput_on_ = static_cast<std::uint8_t>(xinput_on_ & ~bit);
                    h.xinput_rumbling.fetch_and(static_cast<std::uint8_t>(~bit), std::memory_order_acq_rel);
                }
            }
        }
        DWORD timer_ms = xinput_on_ != 0u ? 25u : 0u;
        for (auto& device : devices_) {
            const bool want = run && request.target.kind == RumbleTarget::Kind::Switch &&
                              request.target.slot == device->slot;
            if (service_switch_rumble(*device, want, request.strength, now)) {
                const DWORD period = device->scheme == ControlScheme::SwitchPro2 ? 10u : 25u;
                timer_ms = timer_ms == 0u ? period : std::min(timer_ms, period);
            }
        }
        rumble_timer_ms_ = timer_ms;
    }

    // One Switch controller; true while its motor runs or a change waits for
    // the write gap (the worker keeps its timer).
    bool service_switch_rumble(Device& device, bool want, std::uint8_t strength, std::uint64_t now) {
        const bool pro2 = device.scheme == ControlScheme::SwitchPro2;
        if (device.failed || (pro2 && device.bluetooth)) want = false;
        if (!want && !device.rumble_on) return false;
        const bool changed = want != device.rumble_on || (want && strength != device.rumble_strength);
        const std::uint64_t since = now - device.rumble_sent_us;
        if (!changed && since < (pro2 ? kSwitch2RumbleRefreshUs : kSwitchRumbleRefreshUs)) return true;
        if (!pro2 && device.rumble_sent_us != 0u && since < kSwitchRumbleGapUs) return true;
        if (!device.write_event) device.write_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        std::array<std::uint8_t, 128> report{};
        const std::size_t length = std::min<std::size_t>(device.output_length, report.size());
        const std::span<std::uint8_t> out(report.data(), length);
        const std::size_t built =
            pro2 ? input::rumble::build_switch2_rumble_report(device.rumble_sequence++, want, strength, out)
                 : input::rumble::build_switch_pro_rumble_report(
                       device.packet++, input::rumble::switch_pro_motor_rumble(want, strength), out);
        if (built == 0u) {
            device.rumble_on = false;
            return false;
        }
        Hub& h = hub();
        if (want && device.slot < kMaxSwitchPads) {
            h.switch_rumble_lengths[device.slot].store(static_cast<std::uint16_t>(length), std::memory_order_release);
            h.switch_rumble_pro2[device.slot].store(pro2, std::memory_order_release);
            h.switch_rumble_handles[device.slot].store(device.handle.get(), std::memory_order_release);
        }
        (void)write_report(device.handle.get(), device.write_event.get(), report.data(), static_cast<DWORD>(length), 30);
        device.rumble_sent_us = now;
        device.rumble_on = want;
        device.rumble_strength = strength;
        if (!want && device.slot < kMaxSwitchPads) {
            h.switch_rumble_handles[device.slot].store(nullptr, std::memory_order_release);
        }
        return want;
    }

    void stop_xinput_rumble() {
        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
            if ((xinput_on_ & bit) == 0u) continue;
            XINPUT_VIBRATION off{};
            (void)XInputSetState(slot, &off);
            hub().xinput_rumbling.fetch_and(static_cast<std::uint8_t>(~bit), std::memory_order_acq_rel);
        }
        xinput_on_ = 0u;
        xinput_want_mask_ = 0u;
        xinput_want_strength_ = 0u;
    }

public:
    HANDLE rescan_handle_ = nullptr;

private:
    std::vector<std::unique_ptr<Device>> devices_;
    input::ControlsConfig config_ = input::default_controls_config();
    std::uint64_t config_generation_ = 0;
    DWORD rumble_timer_ms_ = 0;
    std::uint8_t xinput_on_ = 0;        // XInput motors this worker turned on
    std::uint8_t xinput_want_mask_ = 0;
    std::uint8_t xinput_want_strength_ = 0;
};

}  // namespace

void ensure_controller_hid_started() noexcept {
    Hub& h = hub();
    if (h.started.load(std::memory_order_acquire)) return;
    const std::lock_guard<std::mutex> lock(h.lifecycle);
    if (h.started.load(std::memory_order_relaxed)) return;
    try {
        h.stop_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        h.rescan_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        // Created once and never replaced: publish_rumble_request signals it
        // from the emulation thread.
        if (!h.rumble_event) h.rumble_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (h.stop_event.get() == nullptr || h.rescan_event.get() == nullptr || h.rumble_event.get() == nullptr) {
            return;
        }
        static const bool exit_hook = [] {
            // Orderly exits also stop the motors in prepare_for_process_exit
            // (ExitProcess skips atexit).
            std::atexit([] { stop_all_rumble(); });
            return true;
        }();
        (void)exit_hook;
        CM_NOTIFY_FILTER filter{};
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        HidD_GetHidGuid(&filter.u.DeviceInterface.ClassGuid);
        if (CM_Register_Notification(&filter, h.rescan_event.get(), on_device_change, &h.notification) !=
            CR_SUCCESS) {
            h.notification = nullptr;
        }
        HANDLE stop = h.stop_event.get();
        HANDLE rescan = h.rescan_event.get();
        HANDLE rumble = h.rumble_event.get();
        h.worker = std::thread([stop, rescan, rumble] {
            SetThreadDescription(GetCurrentThread(), L"nebula-controller-hid");
            Worker worker;
            worker.rescan_handle_ = rescan;
            worker.run(stop, rescan, rumble);
        });
        h.started.store(true, std::memory_order_release);
    } catch (...) {
    }
}

void stop_controller_hid() noexcept {
    Hub& h = hub();
    const std::lock_guard<std::mutex> lock(h.lifecycle);
    if (!h.started.load(std::memory_order_acquire)) return;
    if (h.notification != nullptr) {
        CM_Unregister_Notification(h.notification);
        h.notification = nullptr;
    }
    SetEvent(h.stop_event.get());
    if (h.worker.joinable()) h.worker.join();
    h.started.store(false, std::memory_order_release);
}

std::size_t read_switch_pads(std::span<SwitchPadState> out, bool consume_gyro) noexcept {
    Hub& h = hub();
    if (!h.started.load(std::memory_order_acquire)) return 0;
    std::size_t count = 0;
    if (consume_gyro) AcquireSRWLockExclusive(&h.lock); else AcquireSRWLockShared(&h.lock);
    for (Published& published : h.pads) {
        if (!published.connected || count >= out.size()) continue;
        out[count++] = published.state;
        if (consume_gyro) {
            published.state.gyro_x = 0.0f;
            published.state.gyro_y = 0.0f;
        }
    }
    if (consume_gyro) ReleaseSRWLockExclusive(&h.lock); else ReleaseSRWLockShared(&h.lock);
    return count;
}

std::uint64_t controller_hid_now_us() noexcept {
    return steady_us();
}

bool rumble_output_disabled() noexcept {
    // Test sessions (hidden window) and trace sessions never drive a motor.
    static const bool disabled = env_flag("GALAXY_HIDE_WINDOW") || env_flag("GALAXY_RUMBLE_TRACE");
    return disabled;
}

void publish_rumble_request(const RumbleRequest& published) noexcept {
    RumbleRequest request = published;
    const RumbleTarget test_target = test_rumble_target();
    if (test_target.kind != RumbleTarget::Kind::None) {
        request.target = test_target;
        request.focused = true;
    }
    Hub& h = hub();
    const std::uint64_t now = steady_us();
    AcquireSRWLockExclusive(&h.rumble_lock);
    const RumbleRequest previous = h.rumble_request;
    h.rumble_request = request;
    h.rumble_published_us = now;
    ReleaseSRWLockExclusive(&h.rumble_lock);
    if (rumble_trace_enabled()) trace_rumble_output(request, now);
    const bool changed = previous.motor_on != request.motor_on || previous.strength != request.strength ||
                         !(previous.target == request.target) || previous.focused != request.focused ||
                         previous.paused != request.paused;
    if (changed && h.started.load(std::memory_order_acquire) && h.rumble_event) SetEvent(h.rumble_event.get());
}

void stop_all_rumble() noexcept {
    Hub& h = hub();
    h.rumble_shutdown.store(true, std::memory_order_release);
    const std::uint8_t xinput = h.xinput_rumbling.exchange(0u, std::memory_order_acq_rel);
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        if ((xinput & (1u << slot)) == 0u) continue;
        XINPUT_VIBRATION off{};
        (void)XInputSetState(slot, &off);
    }
    HANDLE event = nullptr;
    for (std::size_t slot = 0; slot < kMaxSwitchPads; ++slot) {
        if (h.switch_rumble_handles[slot].load(std::memory_order_acquire) == nullptr) continue;
        // Never blocks: the worker holds the lock only to retire a handle,
        // and a crash may have stopped it there.
        if (!TryAcquireSRWLockShared(&h.rumble_device_lock)) continue;
        const HANDLE handle = h.switch_rumble_handles[slot].load(std::memory_order_acquire);
        if (handle != nullptr) {
            if (event == nullptr) event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            (void)write_rumble_off(handle, event, h.switch_rumble_lengths[slot].load(std::memory_order_acquire),
                                   h.switch_rumble_pro2[slot].load(std::memory_order_acquire));
        }
        ReleaseSRWLockShared(&h.rumble_device_lock);
    }
    if (event != nullptr) CloseHandle(event);
}

ControllerHidStatus controller_hid_status() noexcept {
    Hub& h = hub();
    ControllerHidStatus status{};
    status.switch_pro = h.pro_count.load(std::memory_order_acquire);
    status.switch_pro2 = h.pro2_count.load(std::memory_order_acquire);
    status.pro2_needs_winusb = h.pro2_needs_winusb.load(std::memory_order_acquire);
    return status;
}

bool switch_controller_present_now() noexcept {
    try {
        return !enumerate_switch_hid().empty();
    } catch (...) {
        return false;
    }
}

}  // namespace galaxy::host
