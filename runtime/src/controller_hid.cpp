#include "galaxy/controller_hid.h"
#include "galaxy/content_viewport.h"
#include "galaxy/generic_hid.h"
#include "galaxy/playstation_controller.h"
#include "galaxy/rumble.h"
#include "galaxy/steam_controller.h"

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
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "winusb.lib")

namespace galaxy::host {

namespace {

namespace sw = input::switch_hid;
namespace ps = input::playstation;
namespace sv = input::steam;
namespace gh = input::generic_hid;
using input::ControlScheme;
using input::ControllerLink;

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

struct PreparsedFree {
    void operator()(_HIDP_PREPARSED_DATA* data) const noexcept {
        if (data != nullptr) HidD_FreePreparsedData(data);
    }
};
using UniquePreparsed = std::unique_ptr<_HIDP_PREPARSED_DATA, PreparsedFree>;

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
    const char* model_name = "";
    ControllerLink link = ControllerLink::Unknown;
    bool bluetooth = false;
    // Visible to the input consumer. A Steam Controller puck interface stays
    // hidden until a controller connects to it.
    bool published = false;
    bool puck = false;
    bool needs_driver = false;
    UniqueHandle handle;
    UniqueHandle read_event;
    OVERLAPPED read_ov{};
    bool read_pending = false;
    bool resume = false;   // read_event set by hand to continue a burst
    bool failed = false;   // read error (or `gone`): the worker drops it; the setup thread opens it again
    bool gone = false;     // no longer listed
    std::vector<std::uint8_t> buffer;
    USHORT output_length = 64;
    sw::StickCal left{};
    sw::StickCal right{};
    sw::ImuCal imu{};
    sw::Switch2ImuState switch2_imu{};
    sw::SwitchMotion motion{};
    std::uint64_t last_report_us = 0;
    std::uint64_t imu_reports = 0;
    // Motion since the last [controller-motion] line (log_motion): peak acceleration away from 1 g, peak turn
    // rate, shakes started.
    std::uint64_t motion_logged_us = 0;
    float motion_peak_g = 0.0f;
    float motion_peak_rate = 0.0f;
    std::uint32_t motion_shakes = 0;
    std::uint32_t motion_shakes_logged = 0;
    std::unique_ptr<WinUsbPipes> winusb;
    std::size_t slot = kMaxHidPads;  // given when the worker takes it in
    // Output packet number after initialisation (0x10 rumble reports).
    std::uint8_t packet = 0;
    // PlayStation.
    ps::Model ps_model = ps::Model::DualShock4;
    ps::Transport ps_transport = ps::Transport::Usb;
    ps::ImuCal ps_cal{};
    ps::SensorClock ps_clock{};
    bool ps_full_range_rumble = true;
    // Steady-clock microsecond of the light bar write (0: none pending).
    std::uint64_t ps_led_due_us = 0;
    bool ps_led_released = false;
    // Bluetooth: the short report is still arriving; the calibration feature
    // report that switches it to full reports is asked for again, a few times.
    unsigned ps_mode_attempts = 0;
    std::uint64_t ps_mode_next_us = 0;
    // DualSense Bluetooth output report sequence.
    std::uint8_t ps_sequence = 0;
    // Steam controllers: which one, the 2026 one's sensor clock, the 2015 one's shared axes, and the settings that
    // turn their keyboard and mouse off (the Deck's watchdog after its first).
    sv::Model steam_model = sv::Model::Controller;
    sv::SensorClock steam_clock{};
    sv::ClassicState steam_classic{};
    std::uint64_t steam_settings_due_us = 0;
    bool steam_configured = false;
    // Joy-Con: its side (a charging grip's by the controller's own report), the half it is paired with (a pair
    // publishes in its right half's slot), its last state and whether its shoulder (L or R) is held.
    sw::JoyConSide joycon = sw::JoyConSide::None;
    bool grip = false;
    Device* partner = nullptr;
    sw::DecodedState joycon_state{};
    bool joycon_shoulder = false;
    std::uint32_t joycon_buttons = 0;  // held at its last report
    std::uint8_t joycon_presses = 0;
    // Generic HID controller: the parser's description, the layout taken from
    // it and the last values (a device may spread them over several reports).
    UniquePreparsed preparsed;
    gh::Layout generic_layout{};
    gh::Raw generic_raw{};
    std::array<USAGE, gh::kMaxButtons> generic_usages{};
    // Vibration last sent to this controller.
    bool rumble_on = false;
    std::uint8_t rumble_level = 0;
    std::uint8_t rumble_strength = 0;
    std::uint16_t rumble_value = 0;  // PlayStation and Steam motor speed
    std::uint8_t rumble_sequence = 0;
    std::uint64_t rumble_sent_us = 0;
    UniqueHandle write_event;
    // Output sent without waiting (queue_output): the reports waiting, and the one in flight.
    struct Queued {
        std::vector<std::uint8_t> bytes;
        bool feature = false;  // a feature report, else an output report
        bool motor = false;    // a motor's: a newer one replaces it while it waits
    };
    std::deque<Queued> outputs;
    std::vector<std::uint8_t> post_buffer;
    OVERLAPPED post_ov{};
    UniqueHandle post_event;
    bool post_pending = false;
    std::uint64_t post_started_us = 0;
};

struct Published {
    bool connected = false;
    HidPadState state{};
    input::ControllerInfo info{};
};

// What a crashing process sends to stop a controller's motor.
enum class OffKind : std::uint8_t {
    None,
    SwitchPro,
    SwitchPro2,
    DualShock4Usb,
    DualShock4Bluetooth,
    DualSenseUsb,
    DualSenseBluetooth,
    Steam,
    SteamDeck,
};

struct Hub {
    std::mutex lifecycle;
    std::thread worker;
    UniqueHandle stop_event;
    UniqueHandle rescan_event;
    HCMNOTIFICATION notification = nullptr;
    SRWLOCK lock = SRWLOCK_INIT;
    std::array<Published, kMaxHidPads> pads{};
    std::atomic<bool> started{false};
    // Vibration: the game's state from the game thread, and the motors running
    // on the devices (read without locks by stop_all_rumble).
    SRWLOCK rumble_lock = SRWLOCK_INIT;
    RumbleRequest rumble_request{};
    std::uint64_t rumble_published_us = 0;
    UniqueHandle rumble_event;
    // Set by stop_all_rumble (exit, crash): no motor starts again.
    std::atomic<bool> rumble_shutdown{false};
    std::atomic<std::uint8_t> xinput_rumbling{0};  // bit per XInput user index
    // A HID controller's handle while its motor may run; cleared under
    // rumble_device_lock (exclusive) before the handle closes.
    SRWLOCK rumble_device_lock = SRWLOCK_INIT;
    std::array<std::atomic<HANDLE>, kMaxHidPads> hid_rumble_handles{};
    std::array<std::atomic<std::uint16_t>, kMaxHidPads> hid_rumble_lengths{};
    std::array<std::atomic<std::uint8_t>, kMaxHidPads> hid_rumble_off{};
    // Joy-Con halves and pairs (under `lock`) and the Pair Joy-Con screen.
    JoyConStatus joycon{};
    std::atomic<bool> joycon_pairing{false};
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

// A controller's product name for ControllerInfo::model (static storage):
// the first names seen are kept for the process, then a generic one.
const char* intern_model_name(const char* name) noexcept {
    static std::mutex mutex;
    static std::array<std::array<char, 40>, 32> names{};
    static std::size_t count = 0;
    const std::lock_guard<std::mutex> lock(mutex);
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(names[i].data(), name) == 0) return names[i].data();
    }
    if (count == names.size()) return "Game controller";
    std::array<char, 40>& entry = names[count++];
    strncpy_s(entry.data(), entry.size(), name, _TRUNCATE);
    return entry.data();
}

// The HID product string in printable ASCII (the overlay's character set),
// spaces collapsed; "Game controller" when the device has none.
const char* generic_model_name(HANDLE handle) noexcept {
    std::array<wchar_t, 127> wide{};
    char name[40]{};
    std::size_t used = 0;
    if (HidD_GetProductString(handle, wide.data(), static_cast<ULONG>((wide.size() - 1u) * sizeof(wchar_t)))) {
        for (const wchar_t c : wide) {
            if (c == L'\0' || used + 1u >= sizeof(name)) break;
            const bool space = c == L' ' || c == L'\t';
            if (space && (used == 0u || name[used - 1u] == ' ')) continue;
            if (space || (c > 0x20 && c < 0x7f)) name[used++] = space ? ' ' : static_cast<char>(c);
        }
        while (used != 0u && name[used - 1u] == ' ') --used;
        name[used] = '\0';
    }
    return intern_model_name(used != 0u ? name : "Game controller");
}

// A supported HID controller by vendor and product id.
struct ProductInfo {
    ControlScheme scheme = ControlScheme::SwitchPro;
    const char* name = "";
    ps::Model ps_model = ps::Model::DualShock4;
};

std::optional<ProductInfo> find_product(USHORT vendor, USHORT product) noexcept {
    if (vendor == sw::kNintendoVendorId) {
        if (product == sw::kProControllerProductId) return ProductInfo{ControlScheme::SwitchPro, "Switch Pro Controller", {}};
        // Joy-Con: one controller per pair, read as a Pro Controller's halves.
        if (product == sw::kJoyConLeftProductId) return ProductInfo{ControlScheme::SwitchPro, "Joy-Con (L)", {}};
        if (product == sw::kJoyConRightProductId) return ProductInfo{ControlScheme::SwitchPro, "Joy-Con (R)", {}};
        if (product == sw::kChargingGripProductId) return ProductInfo{ControlScheme::SwitchPro, "Joy-Con", {}};
        if (product == sw::kPro2ControllerProductId) {
            return ProductInfo{ControlScheme::SwitchPro2, "Switch 2 Pro Controller", {}};
        }
        return std::nullopt;
    }
    if (const auto model = ps::model_for_product(vendor, product)) {
        return ProductInfo{ControlScheme::PlayStation, ps::model_name(*model), *model};
    }
    if (vendor == sv::kValveVendorId) {
        if (const auto model = sv::model_of_product(product)) {
            return ProductInfo{ControlScheme::Steam,
                               *model == sv::Model::Deck      ? "Steam Deck"
                               : *model == sv::Model::Classic ? "Steam Controller (2015)"
                                                              : "Steam Controller",
                               {}};
        }
    }
    return std::nullopt;
}

// A Joystick or Game Pad collection no other path reads (generic_hid.h).
std::optional<ProductInfo> find_generic(HANDLE probe, USHORT vendor, const std::wstring& path) noexcept {
    if (gh::claimed_elsewhere(vendor, path.c_str())) return std::nullopt;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(probe, &preparsed)) return std::nullopt;
    HIDP_CAPS caps{};
    const bool read = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
    HidD_FreePreparsedData(preparsed);
    if (!read || !gh::is_game_controller(caps.UsagePage, caps.Usage)) return std::nullopt;
    return ProductInfo{ControlScheme::Generic, generic_model_name(probe), {}};
}

// The axes, hat and buttons a generic controller's input reports carry.
gh::Layout generic_layout(PHIDP_PREPARSED_DATA preparsed) {
    gh::Layout layout{};
    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) return layout;
    std::vector<HIDP_VALUE_CAPS> values(caps.NumberInputValueCaps);
    USHORT value_count = caps.NumberInputValueCaps;
    if (value_count != 0u && HidP_GetValueCaps(HidP_Input, values.data(), &value_count, preparsed) == HIDP_STATUS_SUCCESS) {
        for (USHORT i = 0; i < value_count; ++i) {
            const HIDP_VALUE_CAPS& value = values[i];
            if (value.IsRange || value.ReportCount > 1u) continue;
            const USAGE usage = value.NotRange.Usage;
            gh::ValueRange range{true, value.LogicalMin, value.LogicalMax, value.BitSize};
            // A field whose logical maximum overflows LONG reads as negative: take its bit range.
            if (range.max < range.min && value.BitSize > 0u && value.BitSize < 32u) {
                range.min = 0;
                range.max = static_cast<std::int32_t>((1u << value.BitSize) - 1u);
            }
            if (value.UsagePage == gh::kUsagePageGenericDesktop) {
                switch (usage) {
                case gh::kUsageX: layout.axes[static_cast<std::size_t>(gh::Axis::X)] = range; break;
                case gh::kUsageY: layout.axes[static_cast<std::size_t>(gh::Axis::Y)] = range; break;
                case gh::kUsageZ: layout.axes[static_cast<std::size_t>(gh::Axis::Z)] = range; break;
                case gh::kUsageRx: layout.axes[static_cast<std::size_t>(gh::Axis::Rx)] = range; break;
                case gh::kUsageRy: layout.axes[static_cast<std::size_t>(gh::Axis::Ry)] = range; break;
                case gh::kUsageRz: layout.axes[static_cast<std::size_t>(gh::Axis::Rz)] = range; break;
                case gh::kUsageHatSwitch: layout.hat = range; break;
                default: break;
                }
            } else if (value.UsagePage == gh::kUsagePageSimulation) {
                if (usage == gh::kUsageAccelerator) layout.axes[static_cast<std::size_t>(gh::Axis::Accelerator)] = range;
                if (usage == gh::kUsageBrake) layout.axes[static_cast<std::size_t>(gh::Axis::Brake)] = range;
            }
        }
    }
    std::vector<HIDP_BUTTON_CAPS> buttons(caps.NumberInputButtonCaps);
    USHORT button_count = caps.NumberInputButtonCaps;
    if (button_count != 0u &&
        HidP_GetButtonCaps(HidP_Input, buttons.data(), &button_count, preparsed) == HIDP_STATUS_SUCCESS) {
        for (USHORT i = 0; i < button_count; ++i) {
            if (buttons[i].UsagePage != gh::kUsagePageButton) continue;
            const USAGE last = buttons[i].IsRange ? buttons[i].Range.UsageMax : buttons[i].NotRange.Usage;
            layout.buttons = std::max<std::uint16_t>(layout.buttons, static_cast<std::uint16_t>(last));
        }
    }
    return layout;
}

// One report through the HID parser: values it carries replace the held ones.
bool decode_generic(Device& device, std::span<const std::uint8_t> report, sw::DecodedState& decoded) noexcept {
    _HIDP_PREPARSED_DATA* const preparsed = device.preparsed.get();
    if (preparsed == nullptr || report.empty()) return false;
    auto* const bytes = reinterpret_cast<PCHAR>(const_cast<std::uint8_t*>(report.data()));
    const auto size = static_cast<ULONG>(report.size());
    bool any = false;
    ULONG held = static_cast<ULONG>(device.generic_usages.size());
    const NTSTATUS buttons = HidP_GetUsages(HidP_Input, gh::kUsagePageButton, 0, device.generic_usages.data(), &held,
                                            preparsed, bytes, size);
    if (buttons == HIDP_STATUS_SUCCESS || buttons == HIDP_STATUS_BUFFER_TOO_SMALL) {
        std::uint32_t mask = 0;
        for (ULONG i = 0; i < std::min<ULONG>(held, static_cast<ULONG>(device.generic_usages.size())); ++i) {
            const USAGE number = device.generic_usages[i];
            if (number >= 1u && number <= gh::kMaxButtons) mask |= 1u << (number - 1u);
        }
        device.generic_raw.buttons = mask;
        any = true;
    }
    const auto read = [&](USAGE page, USAGE usage, std::uint32_t& out) {
        ULONG value = 0;
        if (HidP_GetUsageValue(HidP_Input, page, 0, usage, &value, preparsed, bytes, size) == HIDP_STATUS_SUCCESS) {
            out = value;
            any = true;
        }
    };
    constexpr std::array<std::pair<USAGE, USAGE>, gh::kAxisCount> kAxisUsages{{
        {gh::kUsagePageGenericDesktop, gh::kUsageX},
        {gh::kUsagePageGenericDesktop, gh::kUsageY},
        {gh::kUsagePageGenericDesktop, gh::kUsageZ},
        {gh::kUsagePageGenericDesktop, gh::kUsageRx},
        {gh::kUsagePageGenericDesktop, gh::kUsageRy},
        {gh::kUsagePageGenericDesktop, gh::kUsageRz},
        {gh::kUsagePageSimulation, gh::kUsageAccelerator},
        {gh::kUsagePageSimulation, gh::kUsageBrake},
    }};
    for (std::size_t i = 0; i < gh::kAxisCount; ++i) {
        if (device.generic_layout.axes[i].present) read(kAxisUsages[i].first, kAxisUsages[i].second, device.generic_raw.axes[i]);
    }
    if (device.generic_layout.hat.present) read(gh::kUsagePageGenericDesktop, gh::kUsageHatSwitch, device.generic_raw.hat);
    if (!any) return false;
    gh::map_report(device.generic_layout, device.generic_raw, decoded);
    return true;
}

struct HidCandidate {
    std::wstring path;
    USHORT vendor = 0;
    USHORT product = 0;
    ProductInfo info{};
};

// USB interface number from "&mi_NN" in a device path (-1: not a composite device).
int interface_number(const std::wstring& path) {
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    const std::size_t at = lower.find(L"&mi_");
    if (at == std::wstring::npos || at + 6u > lower.size()) return -1;
    return static_cast<int>(std::wcstol(lower.substr(at + 4u, 2u).c_str(), nullptr, 16));
}

bool bluetooth_path(const std::wstring& path) {
    // HID over classic Bluetooth, and HID over GATT.
    return contains_ci(path, L"{00001124-0000-1000-8000-00805f9b34fb}") ||
           contains_ci(path, L"{00001812-0000-1000-8000-00805f9b34fb}");
}

ControllerLink link_of(const HidCandidate& candidate) {
    if (candidate.info.scheme == ControlScheme::Steam) {
        if (sv::is_receiver_product(candidate.product)) return ControllerLink::Wireless;
        return candidate.product == sv::kControllerBleProductId ? ControllerLink::Bluetooth : ControllerLink::Usb;
    }
    return bluetooth_path(candidate.path) ? ControllerLink::Bluetooth : ControllerLink::Usb;
}

// A HID collection's compatible ids name its top-level usage
// ("HID_DEVICE_SYSTEM_GAME", "HID_DEVICE_UP:0001_U:0005"): a joystick or game
// pad is found without opening the device.
bool game_collection(HDEVINFO set, SP_DEVINFO_DATA& info) {
    std::array<wchar_t, 1024> ids{};
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_COMPATIBLEIDS, nullptr,
                                           reinterpret_cast<PBYTE>(ids.data()),
                                           static_cast<DWORD>((ids.size() - 2u) * sizeof(wchar_t)), nullptr)) {
        return false;
    }
    for (const wchar_t* id = ids.data(); *id != L'\0'; id += std::wcslen(id) + 1u) {
        const std::wstring text = id;
        if (contains_ci(text, L"HID_DEVICE_SYSTEM_GAME") || contains_ci(text, L"HID_DEVICE_UP:0001_U:0004") ||
            contains_ci(text, L"HID_DEVICE_UP:0001_U:0005")) {
            return true;
        }
    }
    return false;
}

// Every HID interface present: `visit(path, set, info)` for each (the set's device info gives its compatible ids).
template <class Visit>
void for_each_hid_interface(Visit visit) {
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);
    const HDEVINFO set = SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid_guid, index, &iface); ++index) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;
        std::vector<std::uint8_t> storage(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, &info)) continue;
        visit(std::wstring(detail->DevicePath), set, info);
    }
    SetupDiDestroyDeviceInfoList(set);
}

// A HID interface's attributes, opened without access (attributes and the top-level collection only); the probe
// handle in `probe` when asked for.
std::optional<HIDD_ATTRIBUTES> hid_attributes(const std::wstring& path, UniqueHandle* probe = nullptr) {
    UniqueHandle handle(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                    nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) return std::nullopt;
    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(handle.get(), &attributes)) return std::nullopt;
    if (probe != nullptr) *probe = std::move(handle);
    return attributes;
}

// The controllers Steam Input reads for this game (SDL_GAMECONTROLLER_IGNORE_DEVICES, set when Steam starts it):
// read here too, each press would come twice.
const std::string& steam_input_devices() {
    static const std::string list = [] {
        std::array<char, 1024> value{};
        const DWORD length = GetEnvironmentVariableA("SDL_GAMECONTROLLER_IGNORE_DEVICES", value.data(),
                                                     static_cast<DWORD>(value.size()));
        std::string text = length != 0u && length < value.size() ? std::string(value.data(), length) : std::string{};
        if (!text.empty()) std::cerr << "[controller] left to Steam Input: " << text << '\n';
        return text;
    }();
    return list;
}

std::vector<HidCandidate> enumerate_hid_pads() {
    std::vector<HidCandidate> found;
    for_each_hid_interface([&](const std::wstring& path, HDEVINFO set, SP_DEVINFO_DATA& info) {
        // Only the vendors with their own drivers and game collections are opened.
        const bool known_vendor = contains_ci(path, L"057e") || contains_ci(path, L"054c") ||
                                  contains_ci(path, L"28de");
        if (!known_vendor && !game_collection(set, info)) return;
        UniqueHandle probe;
        const std::optional<HIDD_ATTRIBUTES> attributes = hid_attributes(path, &probe);
        if (!attributes) return;
        if (sv::listed_device(steam_input_devices(), attributes->VendorID, attributes->ProductID)) return;
        auto product = find_product(attributes->VendorID, attributes->ProductID);
        if (!product) product = find_generic(probe.get(), attributes->VendorID, path);
        if (!product) return;
        // A Steam controller's other interfaces are its keyboard and mouse (a receiver's, its unused ones).
        if (attributes->VendorID == sv::kValveVendorId &&
            !sv::is_controller_interface(attributes->ProductID, interface_number(path))) {
            return;
        }
        found.push_back({path, attributes->VendorID, attributes->ProductID, *product});
    });
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
        device.needs_driver = true;
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

bool usb_command(Device& device, OverlappedIo& io, std::uint8_t command, bool wait,
                 std::vector<std::uint8_t>* reply = nullptr) {
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
            if (reply != nullptr) reply->assign(in.begin(), in.begin() + got);
            return true;
        }
    }
    return false;
}

void init_switch_pro(Device& device) {
    OverlappedIo io(device.handle.get());
    std::uint8_t packet = 0;
    std::vector<std::uint8_t> data;
    // A Joy-Con names its side (a charging grip's interface holds whichever is in it): by USB status before the
    // handshake, else by the device info subcommand after it. An interface that names none (an empty grip slot) stays
    // no controller.
    const bool joycon = device.grip || device.joycon != sw::JoyConSide::None;
    sw::JoyConSide side = sw::JoyConSide::None;
    if (joycon && !device.bluetooth && usb_command(device, io, sw::kUsbStatus, true, &data) && data.size() >= 4u) {
        side = sw::joycon_side_of_type(data[3]);
    }
    if (device.grip && side == sw::JoyConSide::None) {
        return;  // an empty rail (the setup thread asks again)
    }
    if (!device.bluetooth) {
        if (usb_command(device, io, sw::kUsbHandshake, true)) {
            (void)usb_command(device, io, sw::kUsbHighSpeed, true);
            (void)usb_command(device, io, sw::kUsbHandshake, true);
            (void)usb_command(device, io, sw::kUsbForceHid, false);
        } else {
            device.bluetooth = true;  // no USB acknowledgement
        }
    }
    if (joycon && side == sw::JoyConSide::None &&
        subcommand(device, io, packet, sw::kSubcommandDeviceInfo, {}, 0, &data) && data.size() >= 3u) {
        side = sw::joycon_side_of_type(data[2]);
    }
    if (joycon) {
        device.joycon = side != sw::JoyConSide::None ? side : device.joycon;
        device.model_name = device.joycon == sw::JoyConSide::Left ? "Joy-Con (L)" : "Joy-Con (R)";
    }
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
    std::cerr << "[controller] " << (device.joycon == sw::JoyConSide::Left    ? "joy-con (l)"
                                     : device.joycon == sw::JoyConSide::Right ? "joy-con (r)"
                                                                              : "switch pro")
              << " (" << (device.bluetooth ? "bluetooth" : "usb")
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

// A feature report read on an overlapped handle (IOCTL_HID_GET_FEATURE, as
// HidD_GetFeature cannot wait on one): buffer[0] is the report id. Returns the
// bytes received including the id, 0 on failure or timeout.
DWORD get_feature_report(HANDLE handle, HANDLE event, std::uint8_t* buffer, DWORD size, DWORD timeout_ms) noexcept {
    constexpr DWORD kIoctlHidGetFeature = 0x000b0192;
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || event == nullptr) return 0;
    OVERLAPPED ov{};
    ov.hEvent = event;
    ResetEvent(event);
    DWORD done = 0;
    if (!DeviceIoControl(handle, kIoctlHidGetFeature, buffer, size, buffer, size, nullptr, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) return 0;
        if (WaitForSingleObject(event, timeout_ms) != WAIT_OBJECT_0) {
            CancelIoEx(handle, &ov);
            GetOverlappedResult(handle, &ov, &done, TRUE);
            return 0;
        }
    }
    return GetOverlappedResult(handle, &ov, &done, FALSE) != FALSE ? done : 0;
}

constexpr std::size_t kLargestOutputReport = 600;

// The motor-off report for a HID controller.
bool write_rumble_off(HANDLE handle, HANDLE event, std::uint16_t output_length, OffKind kind) noexcept {
    std::array<std::uint8_t, kLargestOutputReport> report{};
    const std::size_t length = std::min<std::size_t>(output_length, report.size());
    const std::span<std::uint8_t> out(report.data(), length);
    std::size_t built = 0;
    ps::Effects stop;
    stop.apply_rumble = true;
    switch (kind) {
    case OffKind::SwitchPro:
        built = input::rumble::build_switch_pro_rumble_report(0, input::rumble::switch_pro_motor_rumble(0, 0), out);
        break;
    case OffKind::SwitchPro2:
        built = input::rumble::build_switch2_rumble_report(0, 0, 0, out);
        break;
    case OffKind::DualShock4Usb:
        built = ps::build_output_report(ps::Model::DualShock4, ps::Transport::Usb, stop, out);
        break;
    case OffKind::DualShock4Bluetooth:
        built = ps::build_output_report(ps::Model::DualShock4, ps::Transport::Bluetooth, stop, out);
        break;
    case OffKind::DualSenseUsb:
        built = ps::build_output_report(ps::Model::DualSense, ps::Transport::Usb, stop, out);
        break;
    case OffKind::DualSenseBluetooth:
        built = ps::build_output_report(ps::Model::DualSense, ps::Transport::Bluetooth, stop, out);
        break;
    case OffKind::Steam:
        built = sv::build_rumble_report(0, 0, out);
        break;
    case OffKind::SteamDeck: {
        std::array<std::uint8_t, sv::kValveFeatureBytes> feature{};
        return sv::build_deck_rumble_report(0, 0, feature) != 0 &&
               HidD_SetFeature(handle, feature.data(), static_cast<ULONG>(feature.size()));
    }
    case OffKind::None:
        break;
    }
    return built != 0 && write_report(handle, event, report.data(), static_cast<DWORD>(length), 30);
}

OffKind off_kind_of(const Device& device) noexcept {
    switch (device.scheme) {
    case ControlScheme::SwitchPro: return OffKind::SwitchPro;
    case ControlScheme::SwitchPro2: return OffKind::SwitchPro2;
    case ControlScheme::Steam:
        return device.steam_model == sv::Model::Deck      ? OffKind::SteamDeck
               : device.steam_model == sv::Model::Classic ? OffKind::None  // its pulses end by themselves
                                                          : OffKind::Steam;
    case ControlScheme::PlayStation: {
        const bool bluetooth = device.ps_transport == ps::Transport::Bluetooth;
        if (device.ps_model == ps::Model::DualShock4) {
            return bluetooth ? OffKind::DualShock4Bluetooth : OffKind::DualShock4Usb;
        }
        return bluetooth ? OffKind::DualSenseBluetooth : OffKind::DualSenseUsb;
    }
    default: return OffKind::None;
    }
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

// GALAXY_TEST_RUMBLE_TARGET=xinput:N, switch:N, playstation:N or steam:N,
// honoured only while output is disabled: the controller the published request
// names, with the window counted as focused (a hidden window never is).
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
        if (kind == "xinput") {
            parsed.kind = RumbleTarget::Kind::XInput;
            parsed.scheme = ControlScheme::Xbox;
        } else if (kind == "switch") {
            parsed.kind = RumbleTarget::Kind::Hid;
            parsed.scheme = ControlScheme::SwitchPro;
        } else if (kind == "playstation") {
            parsed.kind = RumbleTarget::Kind::Hid;
            parsed.scheme = ControlScheme::PlayStation;
        } else if (kind == "steam") {
            parsed.kind = RumbleTarget::Kind::Hid;
            parsed.scheme = ControlScheme::Steam;
        }
        parsed.slot = static_cast<std::uint8_t>(digit - '0');
        return parsed;
    }();
    return target;
}

// The 0..255 speed of the light motor that PlayStation controllers are driven
// with, as the Xbox light motor is.
std::uint8_t playstation_motor(std::uint8_t level, std::uint8_t strength) noexcept {
    return static_cast<std::uint8_t>(input::rumble::xinput_motor_speeds(level, strength).right >> 8);
}

// A neutral light bar colour, set once.
constexpr std::array<std::uint8_t, 3> kLightBarColor{0x20, 0x20, 0x20};
// A DualSense ignores its light bar until the pairing animation is over.
constexpr std::uint64_t kDualSenseBluetoothLightDelayUs = 4'000'000;

// An output report goes out at the handle's full report length; `report` is
// zero-filled past the `built` meaningful bytes.
bool write_output(Device& device, const std::uint8_t* report, std::size_t built) {
    const std::size_t length = device.output_length;
    if (built == 0 || built > length || length > kLargestOutputReport) return false;
    return write_report(device.handle.get(), device.write_event.get(), report, static_cast<DWORD>(length), 30);
}

// Output sent without waiting for it (the motors, a Steam controller's settings): the worker never stalls on a busy
// radio, so every controller's reports keep arriving as they come. A device's reports go out in order, one in flight
// (the worker waits on its event and sends the next); a motor report replaces one still waiting; one stuck past
// kPostStuckUs is cancelled.
constexpr std::uint64_t kPostStuckUs = 100'000;
constexpr DWORD kIoctlHidSetFeature = 0x000b0191;  // IOCTL_HID_SET_FEATURE
constexpr std::size_t kMostQueued = 8;

void finish_post(Device& device, bool wait) {
    if (!device.post_pending) return;
    DWORD done = 0;
    if (!wait && !GetOverlappedResult(device.handle.get(), &device.post_ov, &done, FALSE) &&
        GetLastError() == ERROR_IO_INCOMPLETE) {
        if (steady_us() - device.post_started_us < kPostStuckUs) return;
        wait = true;
    }
    if (wait) {
        CancelIoEx(device.handle.get(), &device.post_ov);
        GetOverlappedResult(device.handle.get(), &device.post_ov, &done, TRUE);
    }
    device.post_pending = false;
}

void pump_output(Device& device) {
    finish_post(device, false);
    if (!device.post_event && !device.outputs.empty()) device.post_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    while (!device.post_pending && !device.outputs.empty() && device.post_event) {
        Device::Queued next = std::move(device.outputs.front());
        device.outputs.pop_front();
        device.post_buffer = std::move(next.bytes);
        device.post_ov = OVERLAPPED{};
        device.post_ov.hEvent = device.post_event.get();
        ResetEvent(device.post_event.get());
        const DWORD size = static_cast<DWORD>(device.post_buffer.size());
        const BOOL done = next.feature ? DeviceIoControl(device.handle.get(), kIoctlHidSetFeature, device.post_buffer.data(),
                                                         size, nullptr, 0, nullptr, &device.post_ov)
                                       : WriteFile(device.handle.get(), device.post_buffer.data(), size, nullptr,
                                                   &device.post_ov);
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            device.post_pending = true;
            device.post_started_us = steady_us();
        }
    }
}

// `report` (`built` bytes; an output report goes out at the handle's full length) after the device's others; a motor
// report (`motor`) replaces the motor reports waiting unless `replace` is false (the second of a pair).
void queue_output(Device& device, const std::uint8_t* report, std::size_t built, bool feature, bool motor,
                  bool replace = true) {
    const std::size_t length = feature ? built : device.output_length;
    if (built == 0 || built > length || length > kLargestOutputReport) return;
    if (motor && replace) {
        std::erase_if(device.outputs, [](const Device::Queued& queued) { return queued.motor; });
    }
    Device::Queued queued;
    queued.bytes.assign(report, report + built);
    queued.bytes.resize(length, 0u);
    queued.feature = feature;
    queued.motor = motor;
    if (device.outputs.size() >= kMostQueued) device.outputs.pop_front();
    device.outputs.push_back(std::move(queued));
    pump_output(device);
}

bool send_playstation_effects(Device& device, ps::Effects effects) {
    effects.sequence = device.ps_sequence++;
    std::array<std::uint8_t, kLargestOutputReport> report{};
    const std::size_t built = ps::build_output_report(device.ps_model, device.ps_transport, effects, report);
    return write_output(device, report.data(), built);
}

// Reads the calibration (and a DualSense's firmware version); over Bluetooth
// this is also what switches the controller from short to full input reports.
void read_playstation_features(Device& device) {
    std::array<std::uint8_t, 64> feature{};
    const auto read = [&](std::uint8_t id) -> DWORD {
        feature.fill(0);
        feature[0] = id;
        return get_feature_report(device.handle.get(), device.write_event.get(), feature.data(),
                                  static_cast<DWORD>(feature.size()), 200);
    };
    const bool bluetooth = device.ps_transport == ps::Transport::Bluetooth;
    if (device.ps_model == ps::Model::DualShock4) {
        // Over Bluetooth the second report also switches the controller from
        // short to full input reports.
        if (bluetooth) (void)read(ps::kFeatureDualShock4CalibrationUsb);
        const DWORD size = read(bluetooth ? ps::kFeatureDualShock4CalibrationBluetooth
                                          : ps::kFeatureDualShock4CalibrationUsb);
        if (size != 0) device.ps_cal = ps::parse_calibration(std::span<const std::uint8_t>(feature.data(), size), bluetooth);
    } else {
        const DWORD size = read(ps::kFeatureDualSenseCalibration);
        if (size != 0) device.ps_cal = ps::parse_calibration(std::span<const std::uint8_t>(feature.data(), size), false);
        const DWORD firmware = read(ps::kFeatureDualSenseFirmware);
        const std::uint16_t version = firmware != 0 ? ps::dualsense_firmware(std::span<const std::uint8_t>(feature.data(), firmware)) : 0;
        device.ps_full_range_rumble = ps::dualsense_full_range_rumble(device.ps_model, version);
    }
}

void init_playstation(Device& device) {
    read_playstation_features(device);
    std::cerr << "[controller] " << device.model_name << " ("
              << (device.ps_transport == ps::Transport::Bluetooth ? "bluetooth" : "usb") << ") calibration "
              << (device.ps_cal.hardware ? "read" : "default") << '\n';
}

// The light bar, once the first full report has arrived.
void schedule_light_bar(Device& device, std::uint64_t now) {
    if (device.ps_led_due_us != 0u || device.ps_led_released) return;
    const bool slow = device.ps_model != ps::Model::DualShock4 && device.ps_transport == ps::Transport::Bluetooth;
    device.ps_led_due_us = now + (slow ? kDualSenseBluetoothLightDelayUs : 0u) + 1u;
}

void send_light_bar(Device& device) {
    device.ps_led_due_us = 0;
    device.ps_led_released = true;
    if (rumble_output_disabled()) return;
    ps::Effects effects;
    if (device.ps_model != ps::Model::DualShock4) {
        effects.release_led = true;
        (void)send_playstation_effects(device, effects);
        effects = {};
    }
    effects.led = kLightBarColor;
    effects.apply_led = true;
    effects.full_range_rumble = device.ps_full_range_rumble;
    (void)send_playstation_effects(device, effects);
}

// The settings a Steam controller needs from the host: its keyboard and mouse off, its sensors on. The 2026 one and
// the Deck fall back to them unless the settings come again (the Deck's watchdog, every sv::kDeckWatchdogMs); the
// 2015 one keeps them, sent again now and then for a controller that reconnects.
void send_steam_settings(Device& device, std::uint64_t now) {
    const bool deck = device.steam_model == sv::Model::Deck;
    device.steam_settings_due_us = now + static_cast<std::uint64_t>(deck ? sv::kDeckWatchdogMs : sv::kSettingsRefreshMs) * 1000u;
    if (rumble_output_disabled()) return;
    if (device.steam_model == sv::Model::Controller) {
        std::array<std::uint8_t, sv::kFeatureReportBytes> report{};
        const std::size_t built = sv::build_settings_report(report);
        queue_output(device, report.data(), built, true, false);
        return;
    }
    std::array<std::uint8_t, sv::kValveFeatureBytes> report{};
    queue_output(device, report.data(), sv::build_clear_mappings_report(report), true, false);
    const std::size_t built = deck && device.steam_configured ? sv::build_deck_watchdog_report(report)
                                                              : sv::build_valve_settings_report(device.steam_model, report);
    queue_output(device, report.data(), built, true, false);
    device.steam_configured = true;
}

class Worker {
public:
    // The reports, the motors and the pairs, here; listing the controllers and setting up new ones (seconds on a slow
    // radio), on a setup thread of its own, so a controller connecting, a USB device plugged in or a busy radio never
    // holds back another controller's input.
    void run(HANDLE stop, HANDLE rescan, HANDLE rumble) {
        setup_done_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!setup_done_) return;
        setup_ = std::thread([this, stop, rescan] {
            SetThreadDescription(GetCurrentThread(), L"nebula-controller-setup");
            setup_main(stop, rescan);
        });
        service_device_timers();
        for (;;) {
            std::array<HANDLE, 3 + 2 * kMaxHidPads> waits{};
            std::array<Device*, 3 + 2 * kMaxHidPads> owners{};
            std::array<bool, 3 + 2 * kMaxHidPads> writes{};
            DWORD count = 0;
            waits[count++] = stop;
            waits[count++] = setup_done_.get();
            waits[count++] = rumble;
            for (auto& device : devices_) {
                if (device->read_pending) {
                    owners[count] = device.get();
                    writes[count] = false;
                    waits[count++] = device->read_event.get();
                }
                if (device->post_pending) {
                    owners[count] = device.get();
                    writes[count] = true;
                    waits[count++] = device->post_event.get();
                }
            }
            // A timer only while a motor runs or device work is due: it
            // refreshes the motor, stops it when the game's state goes stale
            // and sends deferred writes.
            const DWORD result = WaitForMultipleObjects(count, waits.data(), FALSE, wait_timeout_ms());
            if (result == WAIT_OBJECT_0) break;
            if (result == WAIT_OBJECT_0 + 1) {
                take_setup();
            } else if (result >= WAIT_OBJECT_0 + 3 && result < WAIT_OBJECT_0 + count) {
                Device* device = owners[result - WAIT_OBJECT_0];
                if (writes[result - WAIT_OBJECT_0]) {
                    pump_output(*device);
                } else {
                    complete_read(*device);
                }
            } else if (result != WAIT_OBJECT_0 + 2 && result != WAIT_TIMEOUT) {
                break;  // WAIT_FAILED: leave rather than spin
            }
            drop_failed();
            service_rumble();
            service_device_timers();
        }
        SetEvent(stop);  // the setup thread too
        setup_.join();
        stop_xinput_rumble();
        for (auto& device : devices_) close_device(*device);
        devices_.clear();
        for (auto& device : setup_ready_) close_device(*device);
        setup_ready_.clear();
    }

private:
    void refresh_config() {
        const std::uint64_t generation = input::controls_config_generation();
        if (generation != config_generation_) {
            config_ = input::get_controls_config();
            config_generation_ = generation;
        }
    }

    // The setup thread: at each device change (and every kGripProbeMs while a charging grip has an empty rail), the
    // controllers listed; the new ones opened and set up, then handed to the worker with the paths seen.
    void setup_main(HANDLE stop, HANDLE rescan) {
        std::vector<std::wstring> refused;      // could not be opened: reported once
        std::vector<std::wstring> empty_rails;  // a charging grip's interface with no Joy-Con: asked again
        for (bool first = true;; first = false) {
            if (!first) {
                const HANDLE waits[2] = {stop, rescan};
                const DWORD result =
                    WaitForMultipleObjects(2, waits, FALSE, empty_rails.empty() ? INFINITE : kGripProbeMs);
                if (result != WAIT_OBJECT_0 + 1 && result != WAIT_TIMEOUT) return;
            }
            const std::uint64_t scan_start = steady_us();
            const auto candidates = enumerate_hid_pads();
            std::cerr << "[controller] scan " << candidates.size() << " controller interface(s) in "
                      << (steady_us() - scan_start) / 1000u << " ms\n";
            const auto listed = [&](const std::wstring& path) {
                return std::any_of(candidates.begin(), candidates.end(), [&](const HidCandidate& c) { return c.path == path; });
            };
            // A path missing from this scan is reported again when it returns.
            std::erase_if(refused, [&](const std::wstring& path) { return !listed(path); });
            std::erase_if(empty_rails, [&](const std::wstring& path) { return !listed(path); });
            for (const HidCandidate& candidate : candidates) {
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return;
                bool known = false;
                std::size_t open = 0;
                AcquireSRWLockShared(&setup_lock_);
                known = std::find(open_paths_.begin(), open_paths_.end(), candidate.path) != open_paths_.end();
                open = open_paths_.size();
                ReleaseSRWLockShared(&setup_lock_);
                if (known) continue;
                if (open >= kMaxHidPads) break;
                std::unique_ptr<Device> device = open_device(candidate, refused);
                if (!device) continue;
                const bool rail = std::find(empty_rails.begin(), empty_rails.end(), candidate.path) != empty_rails.end();
                if (device->grip && device->joycon == sw::JoyConSide::None) {
                    if (!rail) {
                        empty_rails.push_back(candidate.path);
                        std::cerr << "[controller] charging grip: a rail with no Joy-Con\n";
                    }
                    continue;  // closed; asked again
                }
                if (rail) std::erase(empty_rails, candidate.path);
                AcquireSRWLockExclusive(&setup_lock_);
                open_paths_.push_back(device->path);
                setup_ready_.push_back(std::move(device));
                ReleaseSRWLockExclusive(&setup_lock_);
            }
            AcquireSRWLockExclusive(&setup_lock_);
            setup_seen_.clear();
            for (const HidCandidate& candidate : candidates) setup_seen_.push_back(candidate.path);
            setup_seen_fresh_ = true;
            ReleaseSRWLockExclusive(&setup_lock_);
            SetEvent(setup_done_.get());
        }
    }

    // What the setup thread found: the devices no longer listed go; the new ones each take a slot, then publish (a
    // Joy-Con half pairs instead), then read.
    void take_setup() {
        std::vector<std::unique_ptr<Device>> ready;
        std::vector<std::wstring> seen;
        bool fresh = false;
        AcquireSRWLockExclusive(&setup_lock_);
        ready.swap(setup_ready_);
        if (setup_seen_fresh_) {
            seen.swap(setup_seen_);
            setup_seen_fresh_ = false;
            fresh = true;
        }
        ReleaseSRWLockExclusive(&setup_lock_);
        if (fresh) {
            for (auto& device : devices_) {
                if (std::find(seen.begin(), seen.end(), device->path) == seen.end()) {
                    device->failed = true;
                    device->gone = true;
                }
            }
            drop_failed();
        }
        for (auto& device : ready) {
            device->slot = free_slot();
            if (device->slot >= kMaxHidPads) {
                forget_path(device->path);
                close_device(*device);
                continue;
            }
            Device& ref = *device;
            devices_.push_back(std::move(device));
            if (ref.joycon == sw::JoyConSide::None && !ref.puck && !ref.grip) {
                publish_device(ref);
            }
            issue_read(ref);
        }
        update_joycon_pairs();
    }

    // Devices whose reads failed or that are no longer listed: their pairs broken, closed and forgotten. A failed
    // read asks the setup thread to look again (a stack that reset opens it again).
    void drop_failed() {
        bool again = false;
        bool halves = false;
        for (auto it = devices_.begin(); it != devices_.end();) {
            Device& device = **it;
            if (!device.failed) {
                ++it;
                continue;
            }
            again = again || !device.gone;
            halves = halves || device.joycon != sw::JoyConSide::None;
            if (Device* partner = device.partner) {
                unpublish_device(*partner);
                partner->partner = nullptr;
                device.partner = nullptr;
            }
            close_device(device);
            forget_path(device.path);
            it = devices_.erase(it);
        }
        if (halves) update_joycon_pairs();
        if (again) SetEvent(rescan_handle_);
    }

    void forget_path(const std::wstring& path) {
        AcquireSRWLockExclusive(&setup_lock_);
        std::erase(open_paths_, path);
        ReleaseSRWLockExclusive(&setup_lock_);
    }

    std::size_t free_slot() const {
        for (std::size_t slot = 0; slot < kMaxHidPads; ++slot) {
            const bool used = std::any_of(devices_.begin(), devices_.end(),
                                          [slot](const auto& d) { return d->slot == slot; });
            if (!used) return slot;
        }
        return kMaxHidPads;
    }

    // On the setup thread: the device opened and set up for its kind (null: it could not be opened).
    static std::unique_ptr<Device> open_device(const HidCandidate& candidate, std::vector<std::wstring>& refused) {
        auto device = std::make_unique<Device>();
        device->path = candidate.path;
        device->scheme = candidate.info.scheme;
        device->model_name = candidate.info.name;
        device->ps_model = candidate.info.ps_model;
        device->link = link_of(candidate);
        device->puck = sv::is_receiver_product(candidate.product);
        device->steam_model = sv::model_of_product(candidate.product).value_or(sv::Model::Controller);
        device->joycon = sw::joycon_side_of_product(candidate.product);
        device->grip = candidate.vendor == sw::kNintendoVendorId && candidate.product == sw::kChargingGripProductId;
        device->handle.reset(CreateFileW(candidate.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OVERLAPPED, nullptr));
        if (device->handle.get() == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (std::find(refused.begin(), refused.end(), candidate.path) == refused.end()) {
                refused.push_back(candidate.path);
                std::cerr << "[controller] " << device->model_name << ": cannot open the device (error " << error
                          << ")\n";
            }
            return nullptr;
        }
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetPreparsedData(device->handle.get(), &preparsed)) {
            (void)HidP_GetCaps(preparsed, &caps);
            if (device->scheme == ControlScheme::Generic) {
                device->generic_layout = generic_layout(preparsed);
                device->preparsed.reset(preparsed);
            } else {
                HidD_FreePreparsedData(preparsed);
            }
        }
        device->buffer.resize(std::max<USHORT>(caps.InputReportByteLength, 64));
        device->output_length = std::max<USHORT>(caps.OutputReportByteLength, 2);
        device->bluetooth = device->link == ControllerLink::Bluetooth;
        device->read_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        device->write_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        switch (device->scheme) {
        case ControlScheme::SwitchPro:
            init_switch_pro(*device);
            break;
        case ControlScheme::SwitchPro2:
            if (device->bluetooth) {
                std::cerr << "[controller] pro controller 2 over bluetooth is not a HID device on Windows; "
                             "connect it by USB\n";
            } else {
                init_switch2(*device);
            }
            break;
        case ControlScheme::PlayStation:
            device->ps_transport = device->bluetooth ? ps::Transport::Bluetooth : ps::Transport::Usb;
            init_playstation(*device);
            break;
        case ControlScheme::Generic:
            std::cerr << "[controller] generic " << device->model_name << ": "
                      << device->generic_layout.buttons << " buttons, axes"
                      << (device->generic_layout.axes[0].present ? " x" : "")
                      << (device->generic_layout.axes[1].present ? " y" : "")
                      << (device->generic_layout.axes[2].present ? " z" : "")
                      << (device->generic_layout.axes[3].present ? " rx" : "")
                      << (device->generic_layout.axes[4].present ? " ry" : "")
                      << (device->generic_layout.axes[5].present ? " rz" : "")
                      << (device->generic_layout.hat.present ? ", hat" : "") << '\n';
            break;
        default:
            break;
        }
        return device;
    }

    void publish_device(Device& device) {
        const std::uint64_t now = steady_us();
        device.published = true;
        device.last_report_us = 0;
        device.steam_clock = {};
        {
            AcquireSRWLockExclusive(&hub().lock);
            Published& published = hub().pads[device.slot];
            published = {};
            published.connected = true;
            published.state.connected = true;
            published.state.scheme = device.scheme;
            published.state.left = device.partner != nullptr ? device.partner->left : device.left;
            published.state.right = device.right;
            published.state.slot = static_cast<std::uint8_t>(device.slot);
            published.info.scheme = device.scheme;
            published.info.slot = static_cast<std::uint8_t>(device.slot);
            // A pair is Bluetooth when either half is.
            published.info.link = device.partner != nullptr && device.partner->link == ControllerLink::Bluetooth
                                      ? ControllerLink::Bluetooth
                                      : device.link;
            published.info.model = device.partner != nullptr ? "Joy-Con (L/R)" : device.model_name;
            published.info.note = device.needs_driver ? "needs WinUSB" : nullptr;
            ReleaseSRWLockExclusive(&hub().lock);
        }
        if (device.scheme == ControlScheme::Steam) {
            device.steam_settings_due_us = now;
            device.steam_configured = false;
        }
        std::cerr << "[controller] connected " << input::control_scheme_id(device.scheme) << ' '
                  << (device.partner != nullptr ? "Joy-Con (L/R)" : device.model_name)
                  << (device.bluetooth ? " bluetooth" : device.puck ? " wireless" : " usb") << '\n';
    }

    // Joy-Con halves pair (sw::pair_joycons): a pair publishes as one controller in its right half's slot; a half
    // without its partner publishes nothing. Then the status the Pair Joy-Con screen shows.
    void update_joycon_pairs() {
        std::vector<Device*> halves;
        for (auto& d : devices_) {
            if (d->joycon != sw::JoyConSide::None && !d->failed) halves.push_back(d.get());
        }
        const auto index_of = [&](const Device* d) {
            const auto it = std::find(halves.begin(), halves.end(), d);
            return it == halves.end() ? -1 : static_cast<int>(it - halves.begin());
        };
        std::vector<sw::JoyConHalf> state(halves.size());
        for (std::size_t i = 0; i < halves.size(); ++i) {
            state[i].side = halves[i]->joycon;
            state[i].partner = index_of(halves[i]->partner);
            state[i].shoulder = halves[i]->joycon_shoulder;
        }
        const bool screen = hub().joycon_pairing.load(std::memory_order_acquire);
        joycon_pairing_seen_ = screen;
        const sw::JoyConPairing pairing = sw::pair_joycons(state, screen);
        if (pairing.made) {
            ++joycon_paired_;
            std::cerr << "[controller] joy-con paired\n";
        }
        if (pairing.changed) {
            // Every half whose partner changed leaves its old pair, then the new pairs publish.
            for (std::size_t i = 0; i < halves.size(); ++i) {
                Device* now = state[i].partner >= 0 ? halves[static_cast<std::size_t>(state[i].partner)] : nullptr;
                if (now != halves[i]->partner) {
                    unpublish_device(*halves[i]);
                    halves[i]->partner = now;
                }
            }
            for (Device* half : halves) {
                if (half->joycon == sw::JoyConSide::Right && half->partner != nullptr && !half->published) {
                    publish_device(*half);
                }
            }
        }
        JoyConStatus status{};
        for (const Device* half : halves) {
            const bool left = half->joycon == sw::JoyConSide::Left;
            status.lefts = static_cast<std::uint8_t>(status.lefts + (left ? 1u : 0u));
            status.rights = static_cast<std::uint8_t>(status.rights + (left ? 0u : 1u));
            status.left_held = status.left_held || (left && half->joycon_shoulder);
            status.right_held = status.right_held || (!left && half->joycon_shoulder);
            status.pairs = static_cast<std::uint8_t>(status.pairs + (!left && half->partner != nullptr ? 1u : 0u));
            std::uint8_t& presses = left ? status.left_presses : status.right_presses;
            presses = static_cast<std::uint8_t>(presses + half->joycon_presses);
        }
        status.paired = joycon_paired_;
        AcquireSRWLockExclusive(&hub().lock);
        hub().joycon = status;
        ReleaseSRWLockExclusive(&hub().lock);
    }

    void unpublish_device(Device& device) {
        if (!device.published) return;
        device.published = false;
        stop_device_rumble(device);
        AcquireSRWLockExclusive(&hub().lock);
        hub().pads[device.slot] = {};
        ReleaseSRWLockExclusive(&hub().lock);
        std::cerr << "[controller] disconnected " << input::control_scheme_id(device.scheme) << ' '
                  << device.model_name << '\n';
    }

    // Motor off on a device that goes away; its crash-stop entry retires first.
    void stop_device_rumble(Device& device) {
        if (device.slot < kMaxHidPads) {
            AcquireSRWLockExclusive(&hub().rumble_device_lock);
            hub().hid_rumble_handles[device.slot].store(nullptr);
            ReleaseSRWLockExclusive(&hub().rumble_device_lock);
        }
        if (device.rumble_on) {
            finish_post(device, true);  // the off report after the last one
            device.outputs.clear();
            if (!device.write_event) device.write_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            (void)write_rumble_off(device.handle.get(), device.write_event.get(), device.output_length,
                                   off_kind_of(device));
            device.rumble_on = false;
        }
    }

    void close_device(Device& device) {
        stop_device_rumble(device);
        finish_post(device, true);
        device.outputs.clear();
        if (device.read_pending && !device.resume) {
            CancelIoEx(device.handle.get(), &device.read_ov);
            DWORD done = 0;
            GetOverlappedResult(device.handle.get(), &device.read_ov, &done, TRUE);
            device.read_pending = false;
        }
        if (device.slot < kMaxHidPads) {
            AcquireSRWLockExclusive(&hub().lock);
            hub().pads[device.slot] = {};
            ReleaseSRWLockExclusive(&hub().lock);
        }
        if (device.published) {
            std::cerr << "[controller] disconnected " << input::control_scheme_id(device.scheme) << ' '
                      << device.model_name << '\n';
        }
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
            // Unplugged or the stack reset: dropped, then opened again if it is still listed.
            device.failed = true;
            return;
        }
        handle_report(device, got);
        issue_read(device);
    }

    // The [controller-motion] log line: after a shake starts (at most once a second), and every 10 s while the
    // controller moves, with the shake thresholds of the sensitivity set and the peaks seen. A log then shows
    // whether the controller's motion arrives and whether it starts shakes.
    static void log_motion(Device& device, std::uint64_t now, const sw::MotionSettings& motion) {
        const bool shook = device.motion_shakes != device.motion_shakes_logged;
        const bool due = now - device.motion_logged_us >= (shook ? 1'000'000ull : 10'000'000ull);
        if (!due || (!shook && device.motion_peak_g < 0.3f)) {
            return;
        }
        std::fprintf(stderr,
                     "[controller-motion] slot=%zu imu-reports=%llu shake=%s sensitivity=%u shakes=%u peak-g=%.2f "
                     "peak-rate=%.1f threshold-g=%.2f threshold-rate=%.1f\n",
                     device.slot, static_cast<unsigned long long>(device.imu_reports), motion.shake ? "on" : "off",
                     static_cast<unsigned>(motion.shake_sensitivity), device.motion_shakes, device.motion_peak_g,
                     device.motion_peak_rate, sw::SwitchMotion::shake_accel_threshold_g(motion.shake_sensitivity),
                     sw::SwitchMotion::shake_gyro_threshold(motion.shake_sensitivity));
        device.motion_logged_us = now;
        device.motion_shakes_logged = device.motion_shakes;
        device.motion_peak_g = 0.0f;
        device.motion_peak_rate = 0.0f;
    }

    bool decode(Device& device, std::span<const std::uint8_t> report, sw::DecodedState& decoded) {
        switch (device.scheme) {
        case ControlScheme::SwitchPro2:
            return sw::decode_switch2_report(report, device.switch2_imu, decoded);
        case ControlScheme::PlayStation:
            return ps::decode_report(device.ps_model, device.ps_transport, report, device.ps_cal, device.ps_clock,
                                     decoded);
        case ControlScheme::Steam: {
            const bool classic = device.steam_model == sv::Model::Classic;
            switch (classic ? sv::decode_classic_wireless_event(report) : sv::decode_wireless_event(report)) {
            case sv::WirelessEvent::Connected:
                if (!device.published) publish_device(device);
                return false;
            case sv::WirelessEvent::Disconnected:
                unpublish_device(device);
                device.steam_classic = {};
                return false;
            case sv::WirelessEvent::None:
                break;
            }
            const bool state = device.steam_model == sv::Model::Deck ? sv::decode_deck_report(report, decoded)
                               : classic ? sv::decode_classic_report(report, device.steam_classic, decoded)
                                         : sv::decode_report(report, device.steam_clock, decoded);
            if (!state) return false;
            if (!device.published) publish_device(device);  // a receiver's controller connected before the device opened
            return true;
        }
        case ControlScheme::Generic:
            return decode_generic(device, report, decoded);
        default:
            return sw::decode_pro_report(report, device.imu, decoded);
        }
    }

    void handle_report(Device& device, DWORD size) {
        const std::span<const std::uint8_t> report(device.buffer.data(), size);
        sw::DecodedState decoded{};
        if (!decode(device, report, decoded)) return;
        // A Joy-Con half keeps its state for its pair, which publishes in the right half's slot; the pointer and the
        // tilt follow the right half alone, a shake of either counts (as a Nunchuk's does on the console).
        Device* target = &device;
        bool pointing = true;
        sw::DecodedState moved = decoded;  // the motion this report carries (a Joy-Con half's own)
        if (device.joycon != sw::JoyConSide::None) {
            const bool left = device.joycon == sw::JoyConSide::Left;
            if (!left) sw::turn_right_joycon_imu(decoded);
            device.joycon_state = decoded;
            const bool held =
                (decoded.buttons & input::pad::bit(left ? input::pad::kLeftShoulderBit : input::pad::kRightShoulderBit)) != 0u;
            // Pairs change by the shoulders only on the Pair Joy-Con screen; its status follows every press and
            // shoulder, so the screen opens on what is held and counted now.
            const bool screen = hub().joycon_pairing.load(std::memory_order_acquire);
            const bool shoulder_changed = held != device.joycon_shoulder;
            const bool pressed = (decoded.buttons & ~device.joycon_buttons) != 0u;
            device.joycon_shoulder = held;
            device.joycon_buttons = decoded.buttons;
            device.joycon_presses = static_cast<std::uint8_t>(device.joycon_presses + (pressed ? 1u : 0u));
            if (screen != joycon_pairing_seen_ || shoulder_changed || pressed) {
                update_joycon_pairs();
            }
            if (device.partner == nullptr) return;
            Device& right = left ? *device.partner : device;
            const Device& left_half = left ? device : *device.partner;
            moved = decoded;
            // The pair's state is the right half's motion (combine_joycons); each half's own feeds its shakes.
            decoded = sw::combine_joycons(left_half.joycon_state, right.joycon_state);
            target = &right;
            pointing = !left;
        }
        refresh_config();
        const input::SchemeTuning& tuning = config_.tune(device.scheme);
        const std::uint64_t now = steady_us();
        float dt_report = device.last_report_us != 0
                              ? static_cast<float>(now - device.last_report_us) * 1e-6f : 0.0f;
        dt_report = std::clamp(dt_report, 0.0f, 0.05f);
        device.last_report_us = now;
        if (device.scheme == ControlScheme::PlayStation) {
            if (device.ps_transport == ps::Transport::Usb || report[0] != ps::kReportState) {
                schedule_light_bar(device, now);
            } else if (device.ps_mode_attempts < kPlayStationModeAttempts && now >= device.ps_mode_next_us) {
                ++device.ps_mode_attempts;
                device.ps_mode_next_us = now + 1'000'000;
                read_playstation_features(device);
            }
        }
        float gyro_x = 0.0f, gyro_y = 0.0f;
        bool shake_started = false;
        sw::MotionSettings motion{};
        motion.shake = tuning.motion_shake != input::MotionShake::Off;
        motion.shake_sensitivity = tuning.shake_sensitivity;
        motion.gyro_pointer = tuning.gyro_pointer && pointing;
        motion.gyro_sensitivity = tuning.gyro_sensitivity;
        motion.picture_aspect = static_cast<float>(galaxy::kWiiContentAspect);
        for (std::size_t i = 0; i < moved.imu_count; ++i) {
            // Switch Pro: three samples 5 ms apart; the others one per report,
            // timed by the controller's clock where it has one.
            const bool pro = device.scheme == ControlScheme::SwitchPro;
            const float dt = pro ? 0.005f : moved.imu_dt > 0.0f ? moved.imu_dt : dt_report;
            const std::uint64_t t = pro ? now - static_cast<std::uint64_t>((moved.imu_count - 1u - i) * 5000u) : now;
            const sw::SwitchMotion::Output out = device.motion.feed(moved.imu[i], dt, t, motion);
            shake_started |= out.shake_started;
            const sw::ImuSample& s = moved.imu[i];
            const float g = std::sqrt(s.accel[0] * s.accel[0] + s.accel[1] * s.accel[1] + s.accel[2] * s.accel[2]);
            const float rate = std::sqrt(s.gyro[0] * s.gyro[0] + s.gyro[1] * s.gyro[1] + s.gyro[2] * s.gyro[2]);
            if (std::isfinite(g) && std::isfinite(rate)) {
                device.motion_peak_g = std::max(device.motion_peak_g, std::fabs(g - 1.0f));
                device.motion_peak_rate = std::max(device.motion_peak_rate, rate);
            }
            device.motion_shakes += out.shake_started ? 1u : 0u;
            gyro_x += out.pointer_x;
            gyro_y += out.pointer_y;
        }
        if (moved.imu_count != 0u) {
            ++device.imu_reports;
        }
        log_motion(device, now, motion);
        AcquireSRWLockExclusive(&hub().lock);
        Published& published = hub().pads[target->slot];
        published.connected = true;
        HidPadState& state = published.state;
        state.connected = true;
        state.scheme = target->scheme;
        state.decoded = decoded;
        state.left = target->partner != nullptr ? target->partner->left : target->left;
        state.right = target->right;
        if (shake_started) state.shake_until_us = now + sw::SwitchMotion::kShakeHoldUs;
        if (pointing) {
            state.gyro_x = std::clamp(state.gyro_x + gyro_x, -4.0f, 4.0f);
            state.gyro_y = std::clamp(state.gyro_y + gyro_y, -4.0f, 4.0f);
            state.imu_live = decoded.imu_count != 0u;
            state.gyro_rest_windows = device.motion.rest_windows();
            state.gravity = device.motion.gravity();
            state.gravity_valid = device.motion.gravity_ready();
        }
        state.slot = static_cast<std::uint8_t>(target->slot);
        ReleaseSRWLockExclusive(&hub().lock);
    }

    // Vibration: applies the game's published motor state (rumble::motor_should_run) to the controller in use
    // and turns every other motor off. Runs after every wake: a device report, a publication, the setup thread's
    // devices, a sent report or the motor timer.
    void service_rumble() {
        Hub& h = hub();
        AcquireSRWLockShared(&h.rumble_lock);
        const RumbleRequest request = h.rumble_request;
        const std::uint64_t published = h.rumble_published_us;
        ReleaseSRWLockShared(&h.rumble_lock);
        const std::uint64_t now = steady_us();
        input::rumble::GateInputs gate{};
        gate.game_motor_on = request.level != 0u;
        gate.now_us = now;
        gate.published_us = published;
        gate.focused = request.focused;
        gate.paused = request.paused;
        gate.strength = request.strength;
        const bool run = !rumble_output_disabled() && !h.rumble_shutdown.load(std::memory_order_acquire) &&
                         input::rumble::motor_should_run(gate);
        const std::uint8_t level = run ? request.level : 0u;
        // XInput motors hold their speed: one call per change. A call that
        // fails (no controller in that slot) is not repeated until the
        // wanted state changes, since XInput calls on empty slots are slow.
        const bool xinput = level != 0u && request.target.kind == RumbleTarget::Kind::XInput &&
                            request.target.slot < XUSER_MAX_COUNT;
        const std::uint8_t want_mask = xinput ? static_cast<std::uint8_t>(1u << request.target.slot) : 0u;
        const std::uint8_t want_level = xinput ? level : 0u;
        const std::uint8_t want_strength = xinput ? request.strength : 0u;
        if (want_mask != xinput_want_mask_ || want_level != xinput_want_level_ ||
            want_strength != xinput_want_strength_) {
            xinput_want_mask_ = want_mask;
            xinput_want_level_ = want_level;
            xinput_want_strength_ = want_strength;
            for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
                const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
                if ((want_mask & bit) != 0u) {
                    const input::rumble::XInputMotors speeds =
                        input::rumble::xinput_motor_speeds(want_level, want_strength);
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
            // A Joy-Con pair is its right half's slot; its left half rumbles with it.
            const std::size_t slot = device->joycon == sw::JoyConSide::Left && device->partner != nullptr
                                         ? device->partner->slot
                                         : device->slot;
            const bool target = request.target.kind == RumbleTarget::Kind::Hid && request.target.slot == slot;
            if (service_hid_rumble(*device, target ? level : std::uint8_t{0}, request.strength, now)) {
                const DWORD period = device->scheme == ControlScheme::SwitchPro2 ? 10u : 25u;
                timer_ms = timer_ms == 0u ? period : std::min(timer_ms, period);
            }
        }
        rumble_timer_ms_ = timer_ms;
    }

    bool service_hid_rumble(Device& device, std::uint8_t level, std::uint8_t strength, std::uint64_t now) {
        switch (device.scheme) {
        case ControlScheme::PlayStation: return service_playstation_rumble(device, level, strength, now);
        case ControlScheme::Steam: return service_steam_rumble(device, level, strength, now);
        case ControlScheme::Generic: return false;  // no standard HID motor report
        default: return service_switch_rumble(device, level, strength, now);
        }
    }

    // Registers the handle for the crash-time stop while a motor may run.
    void arm_crash_stop(Device& device, std::size_t length, OffKind kind) {
        Hub& h = hub();
        h.hid_rumble_lengths[device.slot].store(static_cast<std::uint16_t>(length), std::memory_order_release);
        h.hid_rumble_off[device.slot].store(static_cast<std::uint8_t>(kind), std::memory_order_release);
        h.hid_rumble_handles[device.slot].store(device.handle.get(), std::memory_order_release);
    }

    // One Switch controller; true while its motor runs or a change waits for
    // the write gap (the worker keeps its timer).
    bool service_switch_rumble(Device& device, std::uint8_t level, std::uint8_t strength, std::uint64_t now) {
        const bool pro2 = device.scheme == ControlScheme::SwitchPro2;
        if (device.failed || (pro2 && device.bluetooth)) level = 0u;
        const bool want = level != 0u;
        if (!want && !device.rumble_on) return false;
        const bool changed = want != device.rumble_on ||
                             (want && (level != device.rumble_level || strength != device.rumble_strength));
        const std::uint64_t since = now - device.rumble_sent_us;
        if (!changed && since < (pro2 ? kSwitch2RumbleRefreshUs : kSwitchRumbleRefreshUs)) return true;
        if (!pro2 && device.rumble_sent_us != 0u && since < kSwitchRumbleGapUs) return true;
        if (!device.write_event) device.write_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        std::array<std::uint8_t, 128> report{};
        const std::size_t length = std::min<std::size_t>(device.output_length, report.size());
        const std::span<std::uint8_t> out(report.data(), length);
        const std::size_t built =
            pro2 ? input::rumble::build_switch2_rumble_report(device.rumble_sequence++, level, strength, out)
                 : input::rumble::build_switch_pro_rumble_report(
                       device.packet++, input::rumble::switch_pro_motor_rumble(level, strength), out);
        if (built == 0u) {
            device.rumble_on = false;
            return false;
        }
        if (want) arm_crash_stop(device, length, off_kind_of(device));
        queue_output(device, report.data(), built, false, true);
        device.rumble_sent_us = now;
        device.rumble_on = want;
        device.rumble_level = level;
        device.rumble_strength = strength;
        if (!want && device.slot < kMaxHidPads) {
            hub().hid_rumble_handles[device.slot].store(nullptr, std::memory_order_release);
        }
        return want;
    }

    // PlayStation controllers keep a motor running until told otherwise; a held
    // rumble is repeated now and then, and changes are spaced for Bluetooth.
    bool service_playstation_rumble(Device& device, std::uint8_t level, std::uint8_t strength, std::uint64_t now) {
        if (device.failed) level = 0u;
        const std::uint8_t motor = playstation_motor(level, strength);
        const bool want = motor != 0u;
        if (!want && !device.rumble_on) return false;
        const bool changed = want != device.rumble_on || (want && motor != device.rumble_value);
        const std::uint64_t since = now - device.rumble_sent_us;
        if (!changed && since < kPlayStationRumbleRefreshUs) return true;
        if (device.rumble_sent_us != 0u && since < kPlayStationRumbleGapUs) return true;
        ps::Effects effects;
        effects.rumble_right = motor;
        effects.apply_rumble = true;
        effects.full_range_rumble = device.ps_full_range_rumble;
        effects.sequence = device.ps_sequence++;
        std::array<std::uint8_t, kLargestOutputReport> report{};
        const std::size_t built = ps::build_output_report(device.ps_model, device.ps_transport, effects, report);
        if (built == 0u || built > device.output_length || device.output_length > report.size()) {
            device.rumble_on = false;
            return false;
        }
        if (want) arm_crash_stop(device, device.output_length, off_kind_of(device));
        queue_output(device, report.data(), built, false, true);
        device.rumble_sent_us = now;
        device.rumble_on = want;
        device.rumble_value = motor;
        if (!want) hub().hid_rumble_handles[device.slot].store(nullptr, std::memory_order_release);
        return want;
    }

    // The 2026 Steam Controller stops its motors about 50 ms after the last report, so a held rumble is repeated at
    // sv::kRumbleRepeatMs; the Deck's run until told otherwise (repeated now and then, as PlayStation's); the 2015
    // one pulses its pads' haptics for sv::kClassicPulseUs, repeated at sv::kClassicHapticRepeatMs. All on the light
    // (high-frequency) side.
    bool service_steam_rumble(Device& device, std::uint8_t level, std::uint8_t strength, std::uint64_t now) {
        if (device.failed || !device.published) level = 0u;
        const std::uint16_t speed = input::rumble::xinput_motor_speeds(level, strength).right;
        const bool want = speed != 0u;
        if (!want && !device.rumble_on) return false;
        const bool changed = want != device.rumble_on || (want && speed != device.rumble_value);
        const std::uint64_t since = now - device.rumble_sent_us;
        const std::uint64_t repeat = device.steam_model == sv::Model::Deck      ? kPlayStationRumbleRefreshUs
                                     : device.steam_model == sv::Model::Classic ? sv::kClassicHapticRepeatMs * 1000ull
                                                                                : sv::kRumbleRepeatMs * 1000ull;
        if (!changed && since < repeat) return true;
        if (device.rumble_sent_us != 0u && since < kSteamRumbleGapUs) return true;
        if (device.steam_model == sv::Model::Classic) {
            // No motor: while it runs, both pads pulse for longer the stronger it is; off, the pulses end.
            if (want) {
                std::array<std::uint8_t, sv::kValveFeatureBytes> report{};
                const std::uint16_t on_us = static_cast<std::uint16_t>(
                    300u + static_cast<std::uint32_t>(speed) * (sv::kClassicPulsePeriodUs - 1300u) / 65535u);
                for (std::uint8_t pad = 0; pad < 2; ++pad) {
                    queue_output(device, report.data(), sv::build_classic_haptic_report(pad, on_us, report), true, true,
                                 pad == 0);
                }
            }
        } else if (device.steam_model == sv::Model::Deck) {
            std::array<std::uint8_t, sv::kValveFeatureBytes> report{};
            if (want) arm_crash_stop(device, report.size(), OffKind::SteamDeck);
            queue_output(device, report.data(), sv::build_deck_rumble_report(0, speed, report), true, true);
        } else {
            std::array<std::uint8_t, kLargestOutputReport> report{};
            const std::size_t built = sv::build_rumble_report(0, speed, report);
            if (built == 0u || built > device.output_length || device.output_length > report.size()) {
                device.rumble_on = false;
                return false;
            }
            if (want) arm_crash_stop(device, device.output_length, OffKind::Steam);
            queue_output(device, report.data(), built, false, true);
        }
        device.rumble_sent_us = now;
        device.rumble_on = want;
        device.rumble_value = speed;
        if (!want) hub().hid_rumble_handles[device.slot].store(nullptr, std::memory_order_release);
        return want;
    }

    // Deferred device writes: the PlayStation light bar and the Steam
    // Controller's settings refresh.
    void service_device_timers() {
        const std::uint64_t now = steady_us();
        std::uint64_t soonest = 0;
        const auto consider = [&soonest](std::uint64_t due) {
            if (soonest == 0u || due < soonest) soonest = due;
        };
        for (auto& device : devices_) {
            if (device->failed) continue;
            if (device->ps_led_due_us != 0u) {
                if (now >= device->ps_led_due_us) send_light_bar(*device);
                else consider(device->ps_led_due_us);
            }
            if (device->scheme == ControlScheme::Steam && device->published && device->steam_settings_due_us != 0u) {
                if (now >= device->steam_settings_due_us) send_steam_settings(*device, now);
                consider(device->steam_settings_due_us);
            }
        }
        timer_due_us_ = soonest;
    }

    DWORD wait_timeout_ms() const {
        DWORD timeout = rumble_timer_ms_;
        if (timer_due_us_ != 0u) {
            const std::uint64_t now = steady_us();
            const DWORD until = timer_due_us_ > now ? static_cast<DWORD>((timer_due_us_ - now + 999u) / 1000u) : 1u;
            timeout = timeout == 0u ? until : std::min(timeout, until);
        }
        return timeout != 0u ? timeout : INFINITE;
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
        xinput_want_level_ = 0u;
        xinput_want_strength_ = 0u;
    }

public:
    HANDLE rescan_handle_ = nullptr;

private:
    // Switch output pacing: SDL's hidapi drivers space Pro Controller writes at
    // least ~30 ms apart (faster writes can drop a Bluetooth link) and repeat a
    // held rumble so it does not fade; the Pro Controller 2 takes a fresh motor
    // packet every 12 ms.
    static constexpr std::uint64_t kSwitchRumbleGapUs = 30'000;
    static constexpr std::uint64_t kSwitchRumbleRefreshUs = 40'000;
    static constexpr std::uint64_t kSwitch2RumbleRefreshUs = 12'000;
    // PlayStation: Bluetooth reports take one write per 4 ms interval at most;
    // the motor value persists, so the repeat only guards against a lost write.
    static constexpr std::uint64_t kPlayStationRumbleGapUs = 16'000;
    static constexpr std::uint64_t kPlayStationRumbleRefreshUs = 250'000;
    static constexpr std::uint64_t kSteamRumbleGapUs = 10'000;
    static constexpr unsigned kPlayStationModeAttempts = 5;

    static constexpr DWORD kGripProbeMs = 2000;

    std::vector<std::unique_ptr<Device>> devices_;
    // The setup thread and what it hands over (under setup_lock_): devices ready to read, the paths of the last scan,
    // and every path open here or handed over.
    std::thread setup_;
    UniqueHandle setup_done_;
    SRWLOCK setup_lock_ = SRWLOCK_INIT;
    std::vector<std::unique_ptr<Device>> setup_ready_;
    std::vector<std::wstring> setup_seen_;
    bool setup_seen_fresh_ = false;
    std::vector<std::wstring> open_paths_;
    input::ControlsConfig config_ = input::default_controls_config();
    std::uint64_t config_generation_ = 0;
    DWORD rumble_timer_ms_ = 0;
    // Joy-Con: the Pair Joy-Con screen as the pairs last saw it, and the pairs made.
    bool joycon_pairing_seen_ = false;
    std::uint32_t joycon_paired_ = 0;
    std::uint64_t timer_due_us_ = 0;
    std::uint8_t xinput_on_ = 0;        // XInput motors this worker turned on
    std::uint8_t xinput_want_mask_ = 0;
    std::uint8_t xinput_want_level_ = 0;
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
        // from the game thread.
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

std::size_t read_hid_pads(std::span<HidPadState> out, bool consume_gyro) noexcept {
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

bool hidden_test_session() noexcept {
    static const bool hidden = env_flag("GALAXY_HIDE_WINDOW");
    return hidden;
}

bool rumble_output_disabled() noexcept {
    // Test sessions (hidden window) never drive a motor.
    return hidden_test_session();
}

std::vector<std::wstring> hid_interface_paths(std::uint16_t vendor, std::span<const std::uint16_t> products) {
    std::vector<std::wstring> found;
    try {
        wchar_t vendor_text[8];
        std::swprintf(vendor_text, std::size(vendor_text), L"%04x", vendor);
        for_each_hid_interface([&](const std::wstring& path, HDEVINFO, SP_DEVINFO_DATA&) {
            if (!contains_ci(path, vendor_text)) return;
            const std::optional<HIDD_ATTRIBUTES> attributes = hid_attributes(path);
            if (attributes && attributes->VendorID == vendor &&
                std::find(products.begin(), products.end(), attributes->ProductID) != products.end()) {
                found.push_back(path);
            }
        });
    } catch (...) {
    }
    return found;
}

void publish_rumble_request(const RumbleRequest& published) noexcept {
    RumbleRequest request = published;
    const RumbleTarget test_target = test_rumble_target();
    if (test_target.kind != RumbleTarget::Kind::None) {
        // The vibration setting of that controller type (a replayed session
        // has no controller in use to take it from).
        static std::uint64_t s_generation = 0;
        static std::uint8_t s_strength = input::rumble::kDefaultStrength;
        const std::uint64_t generation = input::controls_config_generation();
        if (generation != s_generation) {
            s_generation = generation;
            const input::SchemeTuning& tuning = input::get_controls_config().tune(test_target.scheme);
            s_strength = tuning.vibration ? tuning.rumble : 0u;
        }
        request.target = test_target;
        request.strength = s_strength;
        request.focused = true;
    }
    Hub& h = hub();
    const std::uint64_t now = steady_us();
    AcquireSRWLockExclusive(&h.rumble_lock);
    const RumbleRequest previous = h.rumble_request;
    h.rumble_request = request;
    h.rumble_published_us = now;
    ReleaseSRWLockExclusive(&h.rumble_lock);
    const bool changed = previous.level != request.level || previous.strength != request.strength ||
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
    for (std::size_t slot = 0; slot < kMaxHidPads; ++slot) {
        if (h.hid_rumble_handles[slot].load(std::memory_order_acquire) == nullptr) continue;
        // Never blocks: the worker holds the lock only to retire a handle,
        // and a crash may have stopped it there.
        if (!TryAcquireSRWLockShared(&h.rumble_device_lock)) continue;
        const HANDLE handle = h.hid_rumble_handles[slot].load(std::memory_order_acquire);
        if (handle != nullptr) {
            if (event == nullptr) event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            (void)write_rumble_off(handle, event, h.hid_rumble_lengths[slot].load(std::memory_order_acquire),
                                   static_cast<OffKind>(h.hid_rumble_off[slot].load(std::memory_order_acquire)));
        }
        ReleaseSRWLockShared(&h.rumble_device_lock);
    }
    if (event != nullptr) CloseHandle(event);
}

std::size_t hid_controllers(std::span<input::ControllerInfo> out) noexcept {
    std::size_t count = 0;
    Hub& h = hub();
    if (h.started.load(std::memory_order_acquire)) {
        AcquireSRWLockShared(&h.lock);
        for (const Published& published : h.pads) {
            if (published.connected && count < out.size()) out[count++] = published.info;
        }
        ReleaseSRWLockShared(&h.lock);
        return count;
    }
    try {
        for (const HidCandidate& candidate : enumerate_hid_pads()) {
            if (count >= out.size()) break;
            input::ControllerInfo& info = out[count];
            info = {};
            info.scheme = candidate.info.scheme;
            info.slot = static_cast<std::uint8_t>(count);
            info.link = link_of(candidate);
            info.model = candidate.info.name;
            ++count;
        }
    } catch (...) {
    }
    return count;
}

JoyConStatus joycon_status() noexcept {
    Hub& h = hub();
    AcquireSRWLockShared(&h.lock);
    const JoyConStatus status = h.joycon;
    ReleaseSRWLockShared(&h.lock);
    return status;
}

void set_joycon_pairing(bool on) noexcept {
    hub().joycon_pairing.store(on, std::memory_order_release);
}

bool hid_controller_present_now() noexcept {
    try {
        return !enumerate_hid_pads().empty();
    } catch (...) {
        return false;
    }
}

}  // namespace galaxy::host
