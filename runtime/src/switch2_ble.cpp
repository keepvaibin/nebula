#include "galaxy/switch2_ble.h"

#include "galaxy/rumble.h"

#include <algorithm>

namespace galaxy::input::switch2_ble {

namespace sw = switch_hid;

std::optional<Advert> parse_advert(std::span<const std::uint8_t> d, std::uint64_t host_address) noexcept {
    if (d.size() < 7u) return std::nullopt;
    if ((d[3] | (d[4] << 8)) != sw::kNintendoVendorId) return std::nullopt;
    const std::uint16_t product = static_cast<std::uint16_t>(d[5] | (d[6] << 8));
    if (product != sw::kJoyCon2LeftProductId && product != sw::kJoyCon2RightProductId &&
        product != sw::kPro2ControllerProductId && product != sw::kGameCubeProductId) {
        return std::nullopt;
    }
    Advert advert;
    advert.product = product;
    if (d.size() < 16u) {
        advert.pairing = true;
        return advert;
    }
    std::uint64_t address = 0;
    for (std::size_t i = 0; i < 6u; ++i) address |= static_cast<std::uint64_t>(d[10 + i]) << (8u * i);
    if (address != 0u && address != host_address) return std::nullopt;
    advert.pairing = address == 0u;
    return advert;
}

std::vector<std::uint8_t> command(std::uint8_t id, std::uint8_t sub, std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> out{id, 0x91, 0x01, sub, 0x00, static_cast<std::uint8_t>(payload.size()), 0x00, 0x00};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::vector<std::uint8_t>> set_up_commands() {
    using Bytes = std::vector<std::uint8_t>;
    const Bytes start_output{0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const Bytes zero{0x00};
    const Bytes features{0x27, 0x00, 0x00, 0x00};  // buttons, sticks, sensors, vibration
    const Bytes vibration{0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x35, 0x00, 0x46, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const Bytes format{0x09, 0x00, 0x00, 0x00};
    const Bytes light{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const Bytes grip{0x01, 0x00, 0x00, 0x00};
    return {
        command(0x03, 0x0d, start_output), command(0x07, 0x01, {}),        command(0x16, 0x01, {}),
        command(0x15, 0x03, zero),         command(0x0c, 0x02, features),  command(0x11, 0x03, {}),
        command(0x0a, 0x08, vibration),    command(0x0c, 0x04, features),  command(0x03, 0x0a, format),
        command(0x09, 0x07, light),        command(0x08, 0x02, grip),
    };
}

std::vector<std::uint8_t> flash_read(std::uint32_t address) {
    const std::array<std::uint8_t, 8> payload{0x40,
                                              0x7e,
                                              0x00,
                                              0x00,
                                              static_cast<std::uint8_t>(address),
                                              static_cast<std::uint8_t>(address >> 8),
                                              static_cast<std::uint8_t>(address >> 16),
                                              static_cast<std::uint8_t>(address >> 24)};
    return command(0x02, 0x04, payload);
}

std::span<const std::uint8_t> flash_block(std::span<const std::uint8_t> reply) noexcept {
    return reply.size() >= 0x50u ? reply.subspan(0x10, 0x40) : std::span<const std::uint8_t>{};
}

std::optional<std::array<std::uint8_t, 64>> usb_report(std::span<const std::uint8_t> value) noexcept {
    if (value.size() < kCommonReportBytes) return std::nullopt;
    std::array<std::uint8_t, 64> report{};
    report[0] = sw::kSwitch2InputReport;
    std::copy_n(value.begin(), kCommonReportBytes, report.begin() + 1);
    return report;
}

std::array<std::uint8_t, kMotorBytes> motor_value(std::uint8_t sequence, std::uint8_t level, std::uint8_t strength) noexcept {
    std::array<std::uint8_t, rumble::kSwitch2RumbleReportBytes> usb{};
    (void)rumble::build_joycon2_rumble_report(sequence, level, strength, usb);
    std::array<std::uint8_t, kMotorBytes> value{};
    value[1] = usb[1];
    for (std::size_t i = 0; i < 3u; ++i) std::copy_n(usb.begin() + 2, 5, value.begin() + 2 + i * 5);
    return value;
}

}  // namespace galaxy::input::switch2_ble
