// SPDX-License-Identifier: GPL-3.0-only
// Mii import (mii_import.h, mii_formats.h): file formats, merging into the console's database, the library's files.

#include "galaxy/mii_formats.h"
#include "galaxy/mii_import.h"
#include "galaxy/wii_device_data.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using galaxy::host::MiiRecord;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

// A Wii Mii with create ID `id` (its first byte 0x80: a console's own, not temporary), its name the ID's digits.
MiiRecord mii(std::uint32_t id, std::uint8_t flags = 0x80u) {
    MiiRecord record{};
    record[0x03] = std::byte{'M'};
    record[0x05] = static_cast<std::byte>('0' + id % 10u);
    record[0x07] = static_cast<std::byte>('0' + id / 10u % 10u);
    record[0x09] = static_cast<std::byte>('0' + id / 100u % 10u);
    record[0x18] = std::byte{flags};
    record[0x19] = static_cast<std::byte>(id >> 16u);
    record[0x1A] = static_cast<std::byte>(id >> 8u);
    record[0x1B] = static_cast<std::byte>(id);
    record[0x1C] = std::byte{0x12};
    return record;
}

// A Wii field (RVLFaceLib RFLi_Endian.h position and width).
unsigned field(const MiiRecord& r, unsigned pos, unsigned len) {
    const std::size_t unit = (pos >> 4u) * 2u;
    const unsigned word = static_cast<unsigned>(r[unit]) << 8u | static_cast<unsigned>(r[unit + 1u]);
    return (word >> (16u - (pos & 15u) - len)) & ((1u << len) - 1u);
}

std::uint16_t stored_crc(const std::vector<std::byte>& db) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(db[db.size() - 2u]) << 8u |
                                      static_cast<std::uint16_t>(db[db.size() - 1u]));
}

std::uint16_t crc_with_zero_field(std::vector<std::byte> bytes, std::size_t field_at) {
    bytes[field_at] = bytes[field_at + 1u] = std::byte{0};
    return galaxy::host::rfl_crc(bytes);
}

void write_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void put16le(std::vector<std::byte>& b, std::size_t at, unsigned v) {
    b[at] = static_cast<std::byte>(v);
    b[at + 1u] = static_cast<std::byte>(v >> 8u);
}

bool wii_formats() {
    using galaxy::host::read_miis;
    bool ok = true;
    std::string error;
    const MiiRecord one = mii(1u);
    ok &= expect(read_miis(one, &error).size() == 1u, "a 74-byte .mii");
    std::vector<std::byte> with_crc(one.begin(), one.end());
    with_crc.push_back(std::byte{0});
    with_crc.push_back(std::byte{0});
    const std::uint16_t crc = galaxy::host::rfl_crc(with_crc);
    with_crc[74] = static_cast<std::byte>(crc >> 8u);
    with_crc[75] = static_cast<std::byte>(crc & 0xFFu);
    ok &= expect(read_miis(with_crc, &error).size() == 1u, "a 76-byte .rsd with its CRC");
    with_crc[75] ^= std::byte{1};
    ok &= expect(read_miis(with_crc, &error).size() == 1u, "a wrong CRC: the game's checks decide");
    ok &= expect(read_miis(std::vector<std::byte>(100u), &error).empty(), "anything else");
    ok &= expect(read_miis(MiiRecord{}, &error).empty(), "an empty Mii");

    std::vector<std::byte> remote(0x2F0u);
    remote[0] = std::byte{'R'};
    remote[1] = std::byte{'N'};
    remote[2] = std::byte{'C'};
    remote[3] = std::byte{'D'};
    const MiiRecord two = mii(2u);
    const MiiRecord three = mii(3u);
    std::copy(two.begin(), two.end(), remote.begin() + 0x08);
    std::copy(three.begin(), three.end(), remote.begin() + 0x08 + 5 * 0x4A);
    ok &= expect(read_miis(remote, &error).size() == 2u, "a Wii Remote's block, empty slots skipped");

    std::vector<std::byte> database = galaxy::host::create_empty_rfl_database();
    ok &= expect(read_miis(database, &error).empty(), "an empty database");
    std::copy(two.begin(), two.end(), database.begin() + 4 + 7 * 0x4A);
    ok &= expect(read_miis(database, &error).size() == 1u, "a database's Miis");

    // A DS Mii: the Wii's 16-bit units and names little-endian.
    MiiRecord wii = mii(7u);
    wii[0x00] = std::byte{0x12};
    wii[0x01] = std::byte{0x34};
    wii[0x22] = std::byte{0xAB};
    wii[0x23] = std::byte{0xC0};
    MiiRecord ds = wii;
    for (const std::size_t unit : {0x00, 0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32, 0x34}) {
        std::swap(ds[unit], ds[unit + 1u]);
    }
    for (std::size_t i = 0; i < 10u; ++i) {
        std::swap(ds[0x02 + 2 * i], ds[0x03 + 2 * i]);
        std::swap(ds[0x36 + 2 * i], ds[0x37 + 2 * i]);
    }
    const auto from_ds = galaxy::host::read_mii_file(ds, "ncd");
    ok &= expect(from_ds.miis.size() == 1u && from_ds.miis[0] == wii, "a DS Mii as the Wii's");
    return ok;
}

bool newer_formats() {
    bool ok = true;
    // A 3DS Mii (Ver3StoreData, 0x60): name "Al", hair 100 (past the Wii's 71), mouth colour 4 (past its 2), eyebrow
    // height 10, a mustache of the 3DS's (5: none and a beard).
    std::vector<std::byte> ver3(0x60);
    ver3[0] = std::byte{3};
    ver3[0x0C] = std::byte{0x90};
    put16le(ver3, 0x1A, 'A');
    put16le(ver3, 0x1C, 'l');
    ver3[0x2E] = std::byte{70};
    ver3[0x2F] = std::byte{60};
    put16le(ver3, 0x32, 100u | (3u << 8u));
    put16le(ver3, 0x3A, 2u | (4u << 5u) | (10u << 9u));
    put16le(ver3, 0x3E, 30u | (4u << 6u));
    put16le(ver3, 0x40, 9u | (5u << 5u));
    const auto from3ds = galaxy::host::read_mii_file(ver3, "cfsd");
    ok &= expect(from3ds.miis.size() == 1u && from3ds.converted == 1u, "a 3DS Mii");
    if (!from3ds.miis.empty()) {
        const MiiRecord& r = from3ds.miis[0];
        ok &= expect(r[0x03] == std::byte{'A'} && r[0x05] == std::byte{'l'}, "its name, big-endian");
        ok &= expect(r[0x16] == std::byte{70} && r[0x17] == std::byte{60}, "its height and build");
        ok &= expect(field(r, 272, 7) == 14u, "a 3DS hair: the Wii's nearest (MiiJS hairs[28])");
        ok &= expect(field(r, 279, 3) == 3u, "its hair colour");
        ok &= expect(field(r, 368, 5) == 10u, "a 3DS mouth: the Wii's nearest (MiiJS mouths[6])");
        ok &= expect(field(r, 373, 2) == 0u, "a mouth colour past the Wii's: its first");
        ok &= expect(field(r, 311, 5) == 10u && field(r, 316, 4) == 4u, "its eyebrow height and spacing");
        ok &= expect(field(r, 400, 2) == 0u && field(r, 402, 2) == 1u, "mustache 5: none and a beard");
        ok &= expect(galaxy::host::mii_placeholder_id(r), "a placeholder create ID: a fresh one on import");
    }

    // A Switch Mii (CharInfo, 0x58): hair colour 8 (the Switch's first black: the 3DS's 0), glasses 12 (the
    // Switch's: the 3DS's 3).
    std::vector<std::byte> char_info(0x58);
    put16le(char_info, 0x10, 'S');
    char_info[0x29] = std::byte{64};
    char_info[0x2A] = std::byte{64};
    char_info[0x32] = std::byte{8};
    char_info[0x41] = std::byte{6};
    char_info[0x4F] = std::byte{12};
    const auto from_switch = galaxy::host::read_mii_file(char_info, "charinfo");
    ok &= expect(from_switch.miis.size() == 1u, "a Switch Mii");
    if (!from_switch.miis.empty()) {
        const MiiRecord& r = from_switch.miis[0];
        ok &= expect(field(r, 279, 3) == 0u && field(r, 384, 4) == 3u, "the Switch's colours and glasses");
        ok &= expect(field(r, 311, 5) == 6u, "its eyebrow height");
    }

    // A Mii Studio code as a link: no name (the Mii Channel's "no name").
    std::array<std::uint8_t, 46> studio{};
    studio[2] = 64;   // build
    studio[16] = 7;   // eyebrow y
    studio[30] = 64;  // height
    std::array<std::uint8_t, 47> code{};
    code[0] = 0x5A;
    for (std::size_t i = 0; i < 46u; ++i) {
        code[i + 1u] = static_cast<std::uint8_t>(((studio[i] ^ code[i]) + 7u) & 0xFFu);
    }
    std::string link = "https://studio.mii.nintendo.com/miis/image.png?data=";
    static constexpr char kHex[] = "0123456789abcdef";
    for (const std::uint8_t b : code) {
        link.push_back(kHex[b >> 4u]);
        link.push_back(kHex[b & 15u]);
    }
    link += "&type=face";
    const std::vector<std::byte> text(reinterpret_cast<const std::byte*>(link.data()),
                                      reinterpret_cast<const std::byte*>(link.data()) + link.size());
    const auto from_studio = galaxy::host::read_mii_file(text, "txt");
    ok &= expect(from_studio.miis.size() == 1u, "a Mii Studio link");
    if (!from_studio.miis.empty()) {
        const MiiRecord& r = from_studio.miis[0];
        ok &= expect(r[0x03] == std::byte{'n'} && r[0x05] == std::byte{'o'} && r[0x07] == std::byte{' '},
                     "no name: the Mii Channel's \"no name\"");
        ok &= expect(field(r, 311, 5) == 7u, "its eyebrow height");
    }
    return ok;
}

bool merging() {
    using galaxy::host::merge_miis;
    bool ok = true;
    std::vector<std::byte> db = galaxy::host::create_empty_rfl_database();
    ok &= expect(galaxy::host::count_miis(db) == 0u, "an empty database holds none");
    std::vector<std::pair<unsigned, MiiRecord>> placed;
    const std::vector<MiiRecord> miis{mii(1u), mii(2u), mii(1u), mii(4u, 0xA0u)};
    const auto r = merge_miis(db, miis, {}, &placed);
    ok &= expect(r.added == 2u && r.duplicates == 1u && r.invalid == 1u && r.no_room == 0u,
                 "two added, a duplicate, a temporary ID refused");
    ok &= expect(placed.size() == 2u && placed[0].first == 0u && placed[1].first == 1u, "the first free slots");
    ok &= expect(galaxy::host::count_miis(db) == 2u, "two held");
    ok &= expect(stored_crc(db) == crc_with_zero_field(db, db.size() - 2u), "the CRC renewed");
    const auto again = merge_miis(db, std::vector<MiiRecord>{mii(2u)}, {}, nullptr);
    ok &= expect(again.added == 0u && again.duplicates == 1u, "importing again adds nothing");
    const auto refused = merge_miis(db, std::vector<MiiRecord>{mii(9u)}, [](MiiRecord&) { return false; }, nullptr);
    ok &= expect(refused.invalid == 1u, "the game refuses it");

    // The same Mii changed (its create ID): over its old self.
    MiiRecord changed = mii(2u);
    changed[0x16] = std::byte{99};
    placed.clear();
    const auto update = merge_miis(db, std::vector<MiiRecord>{changed}, {}, &placed);
    ok &= expect(update.updated == 1u && placed.size() == 1u && placed[0].first == 1u, "a changed Mii updated");

    // Placeholder create IDs (tools write 80000000 00000000): fresh ones, each unique; the same Mii again a duplicate.
    MiiRecord a = mii(0u);
    MiiRecord b = mii(0u);
    for (MiiRecord* m : {&a, &b}) {
        for (std::size_t i = 0x19; i < 0x20; ++i) (*m)[i] = std::byte{0};
    }
    b[0x05] = std::byte{'b'};
    placed.clear();
    const auto fresh = merge_miis(db, std::vector<MiiRecord>{a, b, a}, {}, &placed);
    ok &= expect(fresh.added == 2u && fresh.duplicates == 1u, "two placeholder Miis added, the same one skipped");
    ok &= expect(placed.size() == 2u && !galaxy::host::mii_placeholder_id(placed[0].second) &&
                     !std::equal(placed[0].second.begin() + 0x18, placed[0].second.begin() + 0x20,
                                 placed[1].second.begin() + 0x18),
                 "each a fresh create ID of its own");

    // Favourites: at most the Wii's ten.
    std::vector<MiiRecord> favorites;
    for (std::uint32_t i = 20u; i < 31u; ++i) {
        MiiRecord f = mii(i);
        f[1] |= std::byte{1};
        favorites.push_back(f);
    }
    const auto fav = merge_miis(db, favorites, {}, nullptr);
    ok &= expect(fav.added == 11u && fav.unfavorited == 1u, "the eleventh favourite added as an ordinary Mii");

    std::vector<MiiRecord> many;
    for (std::uint32_t i = 100u; i < 210u; ++i) many.push_back(mii(i));
    const auto full = merge_miis(db, many, {}, nullptr);
    ok &= expect(full.added == 85u && full.no_room == 25u, "100 slots at most");
    ok &= expect(galaxy::host::count_miis(db) == 100u, "full");
    return ok;
}

bool library() {
    bool ok = true;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "nebula_mii_import_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "miis", error);
    const std::filesystem::path db = root / "RFL_DB.dat";
    write_file(db, galaxy::host::create_empty_rfl_database());
    write_file(root / "miis" / "a.mii", mii(11u));
    write_file(root / "miis" / "notes.txt", std::vector<std::byte>(5u, std::byte{'x'}));
    galaxy::host::set_mii_library(db, root / "miis", {});
    std::string message;
    const auto r = galaxy::host::import_mii_folder(&message);
    ok &= expect(r.added == 1u, "the folder's Mii added");
    ok &= expect(message == "ADDED 1, 1 FILE(S) NOT MII DATA", "the message");
    ok &= expect(galaxy::host::mii_library_count() == 1u, "the library holds it");
    const auto installs = galaxy::host::take_mii_installs();
    ok &= expect(installs.size() == 1u && installs[0].first == 0u, "queued for the running game");
    ok &= expect(galaxy::host::take_mii_installs().empty(), "once");
    const auto again = galaxy::host::import_mii_folder(&message);
    ok &= expect(again.added == 0u && again.duplicates == 1u, "the next start adds nothing");
    ok &= expect(galaxy::host::take_mii_installs().empty(), "nothing queued");

    // Files imported from elsewhere: kept in the library folder (once), a different file of a kept name renamed.
    std::filesystem::create_directories(root / "elsewhere" / "other", error);
    const std::filesystem::path b = root / "elsewhere" / "b.mii";
    write_file(b, mii(12u));
    const auto from_elsewhere = galaxy::host::import_mii_files(std::span<const std::filesystem::path>(&b, 1u), &message);
    ok &= expect(from_elsewhere.added == 1u && message == "ADDED 1", "a file from elsewhere added");
    ok &= expect(std::filesystem::exists(root / "miis" / "b.mii"), "and kept in the library folder");
    (void)galaxy::host::import_mii_files(std::span<const std::filesystem::path>(&b, 1u), &message);
    ok &= expect(!std::filesystem::exists(root / "miis" / "b (2).mii"), "the same file kept once");
    const std::filesystem::path other_b = root / "elsewhere" / "other" / "b.mii";
    write_file(other_b, mii(13u));
    (void)galaxy::host::import_mii_files(std::span<const std::filesystem::path>(&other_b, 1u), &message);
    ok &= expect(std::filesystem::exists(root / "miis" / "b (2).mii"), "another file of that name numbered");
    const std::filesystem::path kept_a = root / "miis" / "a.mii";
    (void)galaxy::host::import_mii_files(std::span<const std::filesystem::path>(&kept_a, 1u), &message);
    ok &= expect(!std::filesystem::exists(root / "miis" / "a (2).mii"), "a file in the folder not copied again");
    (void)galaxy::host::take_mii_installs();
    std::filesystem::remove_all(root, error);
    return ok;
}

}  // namespace

int main() {
    const bool ok = wii_formats() & newer_formats() & merging() & library();
    std::cout << (ok ? "mii import tests passed" : "mii import tests FAILED") << '\n';
    return ok ? 0 : 1;
}
