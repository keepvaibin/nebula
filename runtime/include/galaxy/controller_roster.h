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

namespace galaxy::input {

enum class ControllerLink : std::uint8_t { Unknown, Usb, Bluetooth, Wireless };

struct ControllerInfo {
    ControlScheme scheme = ControlScheme::Xbox;
    std::uint8_t slot = 0;  // XInput user index or HID worker slot
    ControllerLink link = ControllerLink::Unknown;
    const char* model = "";  // static storage
    const char* note = nullptr;  // static storage; shown after the name
};

inline constexpr std::uint8_t kKeyboardSlot = 0xff;
inline constexpr std::size_t kMaxControllers = 12;

struct DeviceKey {
    ControlScheme scheme = ControlScheme::Keyboard;
    std::uint8_t slot = kKeyboardSlot;
    friend constexpr bool operator==(DeviceKey, DeviceKey) noexcept = default;
};

struct DeviceEntry {
    DeviceKey key{};
    bool active = false;  // the device the game last took input from
    std::array<char, 56> label{};
};

struct DeviceList {
    std::array<DeviceEntry, kMaxControllers + 1> entries{};
    std::size_t count = 0;
};

// Keyboard and mouse first, then the controllers by scheme and slot (more than
// kMaxControllers are dropped). Controllers of one model are numbered; wired,
// Bluetooth and wireless links are named, then the note. `active` marks the
// entry with that key.
void build_device_list(std::span<const ControllerInfo> connected, std::optional<DeviceKey> active,
                       DeviceList& out) noexcept;

// The entry to edit after the list changed: `previous` if it is still there,
// else another controller of its scheme (same bindings), else the keyboard.
[[nodiscard]] std::size_t resolve_device(const DeviceList& list, DeviceKey previous) noexcept;

}  // namespace galaxy::input
