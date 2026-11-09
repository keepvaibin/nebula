#pragma once

// The input devices the CONTROLS page offers: keyboard and mouse plus every
// connected controller by model name. The host reports what is connected; this
// builds the list and keeps the selection through hot plug. A controller's
// bindings are those of its scheme (controller family).

#include "galaxy/input_bindings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace galaxy::input {

enum class ControllerLink : std::uint8_t { Unknown, Usb, Bluetooth, Wireless };

struct ControllerInfo {
    ControlScheme scheme = ControlScheme::Xbox;
    std::uint32_t slot = 0;  // XInput user index or HID worker slot
    ControllerLink link = ControllerLink::Unknown;
    const char* model = "";  // static storage
    const char* note = nullptr;  // static storage; shown after the name
    // What this one has of what its family can have: sensors for tilt and shake, a gyro among them, a touch surface.
    bool sensors = true;
    bool gyro = true;
    bool touch = true;
    ButtonIconModel icons = ButtonIconModel::Standard;
};

// The keyboard and the phone's screen are the two slots at the top of the 32-bit range; a controller's slot counts up from 0.
inline constexpr std::uint32_t kKeyboardSlot = 0xFFFFFFFFu;
// The controllers the host's sources can report at once: XInput's four user slots (XUSER_MAX_COUNT) and the HID worker's pads
// (kMaxHidPads, the size of the worker's tables). A bound of the sources for the arrays that receive their list
// (connected_controllers), not of DeviceKey (any 32-bit slot) and not a cap of the roster: DeviceList grows to what it is given.
inline constexpr std::size_t kMaxControllers = 4 + 256;

struct DeviceKey {
    ControlScheme scheme = ControlScheme::Keyboard;
    std::uint32_t slot = kKeyboardSlot;
    friend constexpr bool operator==(DeviceKey, DeviceKey) noexcept = default;
};

struct DeviceEntry {
    DeviceKey key{};
    bool active = false;  // the device the game last took input from
    std::array<char, 56> label{};
};

struct DeviceList {
    std::vector<DeviceEntry> entries;
    std::size_t count = 0;  // entries.size()
};

// The phone's own screen: a device with no bindings, the keyboard's scheme with a slot of its own.
inline constexpr std::uint32_t kPhoneSlot = 0xFFFFFFFEu;
[[nodiscard]] constexpr bool is_phone(DeviceKey key) noexcept {
    return key.scheme == ControlScheme::Keyboard && key.slot == kPhoneSlot;
}

// What is there, in the order it is offered: the phone's screen (`phone`), the keyboard and mouse only when one is attached
// (`keyboard`), then every controller by scheme and slot. Controllers of one model
// are numbered; wired, Bluetooth and wireless links are named, then the note. `active` marks the entry with that key.
void build_device_list(std::span<const ControllerInfo> connected, std::optional<DeviceKey> active,
                       DeviceList& out, bool keyboard = true, bool phone = false);

// The entry to edit after the list changed: `previous` if it is still there,
// else another controller of its scheme (same bindings), else the keyboard.
[[nodiscard]] std::size_t resolve_device(const DeviceList& list, DeviceKey previous) noexcept;

}  // namespace galaxy::input
