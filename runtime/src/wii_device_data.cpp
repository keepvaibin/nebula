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

}  // namespace galaxy::host
