// Joy-Con 2, Switch 2 Pro and GameCube controllers over Bluetooth LE: custom GATT services, not HID. A controller in
// pairing mode advertises; the host connects, sends the set-up commands and subscribes to the common input report,
// which is the USB report 0x05 without its id.
#pragma once

#include "galaxy/switch_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace galaxy::input::switch2_ble {

inline constexpr std::uint16_t kManufacturerId = 0x0553;
inline constexpr std::size_t kCommonReportBytes = 63;
inline constexpr std::size_t kMotorBytes = 17;

struct Advert {
    std::uint16_t product = 0;
    bool pairing = false;  // looking for any host
};
// A controller's manufacturer data. Empty for another vendor's or product's, or one paired with a different host.
[[nodiscard]] std::optional<Advert> parse_advert(std::span<const std::uint8_t> data, std::uint64_t host_address) noexcept;

// A command: `command, 0x91, 0x01, sub, 0x00, length, 0x00, 0x00`, then its payload.
[[nodiscard]] std::vector<std::uint8_t> command(std::uint8_t id, std::uint8_t sub, std::span<const std::uint8_t> payload);
// The commands that turn on a controller's reports, sensors and vibration, in order.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> set_up_commands();
[[nodiscard]] std::vector<std::uint8_t> flash_read(std::uint32_t address);
// The flash block in a read's reply (0x40 bytes), empty if the reply is short.
[[nodiscard]] std::span<const std::uint8_t> flash_block(std::span<const std::uint8_t> reply) noexcept;

// The common report's GATT value as report 0x05.
[[nodiscard]] std::optional<std::array<std::uint8_t, 64>> usb_report(std::span<const std::uint8_t> value) noexcept;
// The vibration characteristic's value: 0x00, the sequence nibble, the motor's 5 bytes three times.
[[nodiscard]] std::array<std::uint8_t, kMotorBytes> motor_value(std::uint8_t sequence, std::uint8_t level, std::uint8_t strength) noexcept;

struct Calibration {
    switch_hid::StickCal left{};
    switch_hid::StickCal right{};
    std::array<float, 3> gyro_bias{};
    std::array<std::uint8_t, 2> gamecube_zero{};
};

// One connected controller.
class Link {
public:
    virtual ~Link() = default;
    [[nodiscard]] virtual const Calibration& calibration() const noexcept = 0;
    // `event`, a Windows event, is set when a report arrives or the link is lost.
    virtual void set_event(void* event) noexcept = 0;
    [[nodiscard]] virtual bool next_report(std::array<std::uint8_t, 64>& report) noexcept = 0;
    [[nodiscard]] virtual bool lost() const noexcept = 0;
    virtual void send_motor(std::span<const std::uint8_t, kMotorBytes> value) noexcept = 0;
};

// The controllers connected, as device paths ("ble:" and the address). Looks for them from `start` until `stop`;
// `changed` is set when one connects or leaves.
struct Found {
    std::wstring path;
    std::uint16_t product = 0;
};
void start(void* changed);
void stop();
[[nodiscard]] std::vector<Found> found();
[[nodiscard]] std::shared_ptr<Link> link(const std::wstring& path);
[[nodiscard]] inline bool is_path(const std::wstring& path) noexcept { return path.rfind(L"ble:", 0) == 0; }

}  // namespace galaxy::input::switch2_ble
