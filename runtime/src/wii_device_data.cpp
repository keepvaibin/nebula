// SPDX-License-Identifier: GPL-3.0-only
// Device data for the game (wii_device_data.h).

#include "galaxy/wii_device_data.h"

namespace galaxy::host {
namespace {

void write_be16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::byte>(value >> 8);
    bytes[offset + 1] = static_cast<std::byte>(value & 0xFFu);
}

void write_be32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::byte>(value >> 24);
    bytes[offset + 1] = static_cast<std::byte>((value >> 16) & 0xFFu);
    bytes[offset + 2] = static_cast<std::byte>((value >> 8) & 0xFFu);
    bytes[offset + 3] = static_cast<std::byte>(value & 0xFFu);
}

}  // namespace

std::uint16_t rfl_crc(std::span<const std::byte> bytes) {
    std::uint16_t crc = 0;
    for (const std::byte byte : bytes) {
        std::uint8_t data = static_cast<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit, data <<= 1) {
            if ((crc & 0x8000u) != 0) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021u);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
            if ((data & 0x80u) != 0) {
                crc = static_cast<std::uint16_t>(crc ^ 0x0001u);
            }
        }
    }
    return crc;
}

std::vector<std::byte> create_empty_rfl_database() {
    std::vector<std::byte> db(kRflDatabaseSize, std::byte{0});
    write_be32(db, 0x0000, kRflOfficialMagic);
    // Copyright-neutral: a valid empty FaceLib container with no face data.
    write_be32(db, kRflDatabaseIsolationOffset, 0x80000000u);
    write_be32(db, kRflDatabaseHiddenOffset + 0x00, kRflHiddenMagic);
    write_be16(db, kRflDatabaseHiddenOffset + 0x04, 0xFFFFu);
    write_be16(db, kRflDatabaseHiddenOffset + 0x06, 0xFFFFu);
    for (std::size_t i = 0; i < kRflHiddenTableEntries; ++i) {
        const std::size_t offset =
            kRflHiddenTableOffset + i * kRflHiddenTableEntrySize;
        write_be16(db, offset + 0x08, 0x7FFFu);
        write_be16(db, offset + 0x0A, 0x7FFFu);
    }
    write_be16(db, kRflDatabaseSize - 2, 0);
    write_be16(db, kRflDatabaseSize - 2, rfl_crc(db));
    return db;
}

std::array<std::uint8_t, kWiimoteEepromBytes> create_virtual_wiimote_eeprom() {
    std::array<std::uint8_t, kWiimoteEepromBytes> eeprom{};
    // The SDK reads two duplicate 11-byte DPD blocks, then two duplicate 10-byte accelerometer
    // blocks. A neutral virtual sensor bar and a checksum-valid upright accelerometer calibration
    // let KPAD trust the IR data.
    const std::array<std::uint8_t, 11> ir_calibration{
        0x7f, 0xa2, 0x8b, 0x80, 0xa2, 0x80, 0x5d, 0x30, 0x7f, 0x5d, 0x0c};
    for (std::size_t i = 0; i < ir_calibration.size(); ++i) {
        eeprom[i] = ir_calibration[i];
        eeprom[0x0Bu + i] = ir_calibration[i];
    }
    const std::array<std::uint8_t, 10> accel_calibration{
        0x80, 0x80, 0x80, 0x00, 0x99, 0x99, 0x99, 0x00, 0x00, 0xa0};
    for (std::size_t i = 0; i < accel_calibration.size(); ++i) {
        eeprom[0x16u + i] = accel_calibration[i];
        eeprom[0x20u + i] = accel_calibration[i];
    }
    return eeprom;
}

std::array<std::uint8_t, 256> create_virtual_nunchuk_registers(
    bool initialized) {
    std::array<std::uint8_t, 256> regs{};
    regs.fill(0xff);
    if (!initialized) {
        return regs;
    }

    regs.fill(0);
    // A real RVL-CNT-01 Nunchuk's calibration: accelerometer 0G/1G, stick max/min/centre. Reports stay
    // centred on these values: a title screen that needs a neutral Control Stick accepts the device only then.
    const std::array<std::uint8_t, 16> calibration{
        0x7d, 0x7e, 0x7a, 0x3c, 0xb1, 0xaf, 0xae, 0x1b,
        0xe0, 0x15, 0x7b, 0xe6, 0x1d, 0x81, 0x23, 0x78};
    for (std::size_t i = 0; i < calibration.size(); ++i) {
        regs[0x20u + i] = calibration[i];
    }
    const std::array<std::uint8_t, 6> nunchuk_id{
        0x00, 0x00, 0xa4, 0x20, 0x00, 0x00};
    for (std::size_t i = 0; i < nunchuk_id.size(); ++i) {
        regs[0xFAu + i] = nunchuk_id[i];
    }
    return regs;
}

}  // namespace galaxy::host
