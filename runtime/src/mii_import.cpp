// SPDX-License-Identifier: GPL-3.0-only
// Mii import (mii_import.h).

#include "galaxy/mii_import.h"

#include "galaxy/atomic_file.h"
#include "galaxy/mii_formats.h"
#include "galaxy/wii_device_data.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>

namespace galaxy::host {
namespace {

constexpr std::size_t kDatabaseFirst = 0x04;
constexpr std::size_t kCreateId = 0x18;              // RFLiCharData::createID, 8 bytes
constexpr std::uint8_t kTemporaryId = 0x20;          // RFLi_CREATE_ID_MASK_TEMPORARY, in the create ID's first byte
constexpr std::uint32_t kNebulaSystemId = 0x4E45424Cu;  // "NEBL": the console part of create IDs made here

std::span<const std::byte> create_id(std::span<const std::byte> record) { return record.subspan(kCreateId, 8u); }

bool has_id(std::span<const std::byte> record) {
    const auto id = create_id(record);
    return std::any_of(id.begin(), id.end(), [](std::byte b) { return b != std::byte{0}; });
}

bool stored_id(const MiiRecord& record) {
    return has_id(record) && (static_cast<std::uint8_t>(record[kCreateId]) & kTemporaryId) == 0u;
}

bool same_id(std::span<const std::byte> a, std::span<const std::byte> b) {
    return std::equal(create_id(a).begin(), create_id(a).end(), create_id(b).begin());
}

// The same Mii whatever its create ID (one exported twice by a tool that writes none) and favourite bit (one past
// the ten favourites was imported without it).
bool same_mii(std::span<const std::byte> a, std::span<const std::byte> b) {
    return a[0] == b[0] && (a[1] & std::byte{0xFE}) == (b[1] & std::byte{0xFE}) &&
           std::equal(a.begin() + 2, a.begin() + kCreateId, b.begin() + 2) &&
           std::equal(a.begin() + kCreateId + 8, a.end(), b.begin() + kCreateId + 8);
}

// RFLiCharData's favourite bit: the first unit's least significant (RFLi_Endian.h favorite, bit 15).
bool favorite(std::span<const std::byte> record) { return (static_cast<std::uint8_t>(record[1]) & 1u) != 0u; }

void renew_crc(std::vector<std::byte>& database) {
    database[kRflDatabaseSize - 2u] = std::byte{0};
    database[kRflDatabaseSize - 1u] = std::byte{0};
    const std::uint16_t crc = rfl_crc(database);
    database[kRflDatabaseSize - 2u] = static_cast<std::byte>(crc >> 8u);
    database[kRflDatabaseSize - 1u] = static_cast<std::byte>(crc & 0xFFu);
}

std::uint32_t load_be32(std::span<const std::byte> b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) << 24u | static_cast<std::uint32_t>(b[at + 1u]) << 16u |
           static_cast<std::uint32_t>(b[at + 2u]) << 8u | static_cast<std::uint32_t>(b[at + 3u]);
}

void store_be32(MiiRecord& r, std::size_t at, std::uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) r[at + i] = static_cast<std::byte>(v >> (24u - 8u * i));
}

bool is_database(std::span<const std::byte> bytes) {
    return bytes.size() == kRflDatabaseSize && load_be32(bytes, 0u) == kRflOfficialMagic;
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::transform(chars.begin(), chars.end(), bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

struct Library {
    std::mutex mutex;
    std::filesystem::path database;
    std::filesystem::path folder;
    MiiPreparer prepare;
    std::vector<std::pair<unsigned, MiiRecord>> installs;
    std::atomic<bool> pending{false};
};

Library& library() {
    static Library instance;
    return instance;
}

// A copy of a file Miis came from, kept in the library folder that every start imports again: none for a file already
// there or one of the same bytes already kept; another file of the same name gets a numbered one ("name (2).mii").
bool keep_in_library(const std::filesystem::path& folder, const std::filesystem::path& file,
                     std::span<const std::byte> bytes) {
    std::error_code error;
    if (folder.empty()) {
        return false;
    }
    std::filesystem::create_directories(folder, error);
    if (std::filesystem::equivalent(file.parent_path(), folder, error)) {
        return true;
    }
    for (unsigned n = 1u; n < 1000u; ++n) {
        std::filesystem::path name = file.stem();
        if (n > 1u) {
            name += " (" + std::to_string(n) + ")";
        }
        name += file.extension();
        const std::filesystem::path target = folder / name;
        if (!std::filesystem::exists(target, error)) {
            return io::write_text_file_atomically(
                target, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        }
        const std::vector<std::byte> kept = read_file(target);
        if (std::equal(kept.begin(), kept.end(), bytes.begin(), bytes.end())) {
            return true;
        }
    }
    return false;
}

std::string result_text(const MiiMergeResult& r) {
    std::string text = "ADDED " + std::to_string(r.added);
    if (r.updated != 0u) text += ", UPDATED " + std::to_string(r.updated);
    if (r.duplicates != 0u) text += ", " + std::to_string(r.duplicates) + " ALREADY HERE";
    if (r.invalid != 0u) text += ", " + std::to_string(r.invalid) + " NOT VALID";
    if (r.no_room != 0u) text += ", " + std::to_string(r.no_room) + " NO ROOM (100 MAX)";
    if (r.unfavorited != 0u) text += ", " + std::to_string(r.unfavorited) + " NOT FAVOURITES (10 MAX)";
    if (r.converted != 0u) text += ", " + std::to_string(r.converted) + " FROM NEWER CONSOLES";
    return text;
}

}  // namespace

std::vector<MiiRecord> read_miis(std::span<const std::byte> bytes, std::string* error) {
    MiiFileContents contents = read_mii_file(bytes, {});
    if (error != nullptr) *error = contents.error;
    return std::move(contents.miis);
}
MiiMergeResult merge_miis(std::vector<std::byte>& database, std::span<const MiiRecord> miis, const MiiPreparer& prepare,
                          std::vector<std::pair<unsigned, MiiRecord>>* placed) {
    MiiMergeResult result;
    if (!is_database(database)) {
        result.no_room = static_cast<unsigned>(miis.size());
        return result;
    }
    const auto slot_bytes = [&database](std::size_t slot) {
        return std::span<std::byte>(database).subspan(kDatabaseFirst + slot * kMiiRecordSize, kMiiRecordSize);
    };
    // Fresh create IDs: the Wii's Mii ID (its type bits, then 4-second units since 2006-01-01) and this console's.
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    std::uint32_t next_time = static_cast<std::uint32_t>(
        ((std::chrono::duration_cast<std::chrono::seconds>(now).count() - 1136073600) / 4) & 0x1FFFFFFF);
    bool changed = false;
    for (MiiRecord mii : miis) {
        if (!stored_id(mii) || (prepare && !prepare(mii))) {
            ++result.invalid;
            continue;
        }
        const bool placeholder = mii_placeholder_id(mii);
        std::size_t id_slot = kMiiDatabaseSlots;
        std::size_t free_slot = kMiiDatabaseSlots;
        bool duplicate = false;
        unsigned favorites = 0u;
        for (std::size_t slot = 0; slot < kMiiDatabaseSlots; ++slot) {
            const auto existing = slot_bytes(slot);
            if (!has_id(existing)) {
                free_slot = std::min(free_slot, slot);
                continue;
            }
            favorites += favorite(existing) ? 1u : 0u;
            duplicate = duplicate || same_mii(existing, mii);
            if (same_id(existing, mii)) id_slot = slot;
        }
        if (duplicate) {
            ++result.duplicates;
            continue;
        }
        if (id_slot != kMiiDatabaseSlots && !placeholder) {
            // The same Mii (a console's create ID), changed: over its old self, where saves that use it point.
            if (favorite(mii) && !favorite(slot_bytes(id_slot)) && favorites >= kMiiFavoriteLimit) {
                mii[1] &= std::byte{0xFE};
                ++result.unfavorited;
            }
            std::copy(mii.begin(), mii.end(), slot_bytes(id_slot).begin());
            ++result.updated;
            changed = true;
            if (placed != nullptr) placed->emplace_back(static_cast<unsigned>(id_slot), mii);
            continue;
        }
        if (free_slot == kMiiDatabaseSlots) {
            ++result.no_room;
            continue;
        }
        if (placeholder || id_slot != kMiiDatabaseSlots) {
            // A create ID of its own: its type bits kept (special or not), unique in the database.
            const std::uint32_t type = load_be32(mii, kCreateId) & 0xE0000000u;
            for (;;) {
                store_be32(mii, kCreateId, (type != 0u ? type : 0x80000000u) | (next_time++ & 0x1FFFFFFFu));
                store_be32(mii, kCreateId + 4u, kNebulaSystemId);
                bool taken = false;
                for (std::size_t slot = 0; slot < kMiiDatabaseSlots && !taken; ++slot) {
                    taken = same_id(slot_bytes(slot), mii);
                }
                if (!taken) break;
            }
        }
        if (favorite(mii) && favorites >= kMiiFavoriteLimit) {
            mii[1] &= std::byte{0xFE};
            ++result.unfavorited;
        }
        std::copy(mii.begin(), mii.end(), slot_bytes(free_slot).begin());
        ++result.added;
        changed = true;
        if (placed != nullptr) placed->emplace_back(static_cast<unsigned>(free_slot), mii);
    }
    if (changed) renew_crc(database);
    return result;
}

unsigned count_miis(std::span<const std::byte> database) {
    if (!is_database(database)) return 0u;
    unsigned count = 0u;
    for (std::size_t slot = 0; slot < kMiiDatabaseSlots; ++slot) {
        count += has_id(database.subspan(kDatabaseFirst + slot * kMiiRecordSize, kMiiRecordSize)) ? 1u : 0u;
    }
    return count;
}

void set_mii_library(const std::filesystem::path& database, const std::filesystem::path& import_folder,
                     MiiPreparer prepare) {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    lib.database = database;
    lib.folder = import_folder;
    lib.prepare = std::move(prepare);
}

std::filesystem::path mii_import_folder() {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    return lib.folder;
}

MiiMergeResult import_mii_files(std::span<const std::filesystem::path> files, std::string* message) {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    MiiMergeResult total;
    if (lib.database.empty()) {
        if (message != nullptr) *message = "NO MII DATABASE";
        return total;
    }
    std::vector<std::byte> database = read_file(lib.database);
    if (!is_database(database)) {
        if (message != nullptr) *message = "THE CONSOLE'S MII DATABASE IS DAMAGED";
        return total;
    }
    unsigned unreadable = 0u;
    unsigned unkept = 0u;
    std::vector<std::pair<unsigned, MiiRecord>> placed;
    for (const std::filesystem::path& file : files) {
        const std::vector<std::byte> bytes = read_file(file);
        const MiiFileContents contents = read_mii_file(bytes, lower_extension(file));
        if (contents.miis.empty()) {
            ++unreadable;
            continue;
        }
        if (!keep_in_library(lib.folder, file, bytes)) {
            ++unkept;
        }
        const MiiMergeResult r = merge_miis(database, contents.miis, lib.prepare, &placed);
        total.added += r.added;
        total.updated += r.updated;
        total.duplicates += r.duplicates;
        total.invalid += r.invalid;
        total.no_room += r.no_room;
        total.unfavorited += r.unfavorited;
        total.converted += contents.converted;
    }
    if (!placed.empty()) {
        const std::string_view contents(reinterpret_cast<const char*>(database.data()), database.size());
        if (!io::write_text_file_atomically(lib.database, contents)) {
            if (message != nullptr) *message = "COULD NOT WRITE THE MII DATABASE";
            return MiiMergeResult{};
        }
        lib.installs.insert(lib.installs.end(), placed.begin(), placed.end());
        lib.pending.store(true, std::memory_order_release);
    }
    if (message != nullptr) {
        *message = result_text(total);
        if (unreadable != 0u) *message += ", " + std::to_string(unreadable) + " FILE(S) NOT MII DATA";
        if (unkept != 0u) *message += ", " + std::to_string(unkept) + " FILE(S) NOT COPIED TO THE MIIS FOLDER";
    }
    return total;
}

MiiMergeResult import_mii_folder(std::string* message) {
    const std::filesystem::path folder = mii_import_folder();
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!folder.empty() && std::filesystem::is_directory(folder, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            if (entry.is_regular_file(error)) files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        if (message != nullptr) *message = "NO FILES";
        return {};
    }
    return import_mii_files(files, message);
}

unsigned mii_library_count() {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    if (lib.database.empty()) return 0u;
    return count_miis(read_file(lib.database));
}

std::vector<std::pair<unsigned, MiiRecord>> take_mii_installs() {
    Library& lib = library();
    if (!lib.pending.load(std::memory_order_acquire)) return {};
    const std::lock_guard<std::mutex> lock(lib.mutex);
    lib.pending.store(false, std::memory_order_relaxed);
    return std::exchange(lib.installs, {});
}

}  // namespace galaxy::host
