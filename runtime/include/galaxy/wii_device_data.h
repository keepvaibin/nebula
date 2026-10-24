// SPDX-License-Identifier: GPL-3.0-only
// Data the console's devices hold, as the host gives it to the game: an empty Mii database (FaceLib), a virtual Wii
// Remote's EEPROM and a Nunchuk's registers (calibration and identity).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::host {

inline constexpr std::uint32_t kRflOfficialMagic = 0x524E4F44;  // "RNOD"
inline constexpr std::uint32_t kRflHiddenMagic = 0x524E4844;    // "RNHD"
inline constexpr std::size_t kRflDatabaseIsolationOffset = 0x1CEC;
inline constexpr std::size_t kRflDatabaseHiddenOffset = 0x1D00;
inline constexpr std::size_t kRflHiddenTableOffset = kRflDatabaseHiddenOffset + 0x08;
inline constexpr std::size_t kRflHiddenTableEntrySize = 0x0C;
inline constexpr std::size_t kRflHiddenTableEntries = 10000;
inline constexpr std::size_t kRflDatabaseSize = 0x1F1E0;
inline constexpr std::size_t kWiimoteEepromBytes = 0x4000;

// FaceLib's CRC-16 over a database image.
[[nodiscard]] std::uint16_t rfl_crc(std::span<const std::byte> bytes);
// A valid empty FaceLib database (no faces).
[[nodiscard]] std::vector<std::byte> create_empty_rfl_database();
// A virtual Wii Remote's EEPROM: IR and accelerometer calibration.
[[nodiscard]] std::array<std::uint8_t, kWiimoteEepromBytes> create_virtual_wiimote_eeprom();
// A Nunchuk's extension registers (0xFF until initialized).
[[nodiscard]] std::array<std::uint8_t, 256> create_virtual_nunchuk_registers(bool initialized);

}  // namespace galaxy::host
