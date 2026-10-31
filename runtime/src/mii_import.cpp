// SPDX-License-Identifier: GPL-3.0-only
// The Mii library and Mii import (mii_import.h).

#include "galaxy/mii_import.h"

#include "galaxy/atomic_file.h"
#include "galaxy/mii_formats.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <mutex>

namespace galaxy::host {
namespace {

constexpr std::string_view kLibraryFormat = "nebula-mii-library 1";
constexpr std::uint8_t kTemporaryId = 0x20;          // the create ID's temporary bit, in its first byte
constexpr std::uint32_t kNebulaSystemId = 0x4E45424Cu;  // "NEBL": the console part of create IDs made here

// The library file's numeric fields, by name.
struct NumberField {
    std::string_view name;
    std::uint8_t MiiFields::*member;
};
constexpr NumberField kNumbers[] = {
    {"sex", &MiiFields::sex},
    {"birth_month", &MiiFields::birth_month},
    {"birth_day", &MiiFields::birth_day},
    {"favorite_color", &MiiFields::favorite_color},
    {"favorite", &MiiFields::favorite},
    {"local_only", &MiiFields::local_only},
    {"height", &MiiFields::height},
    {"build", &MiiFields::build},
    {"face_type", &MiiFields::face_type},
    {"face_color", &MiiFields::face_color},
    {"face_texture", &MiiFields::face_texture},
    {"hair_type", &MiiFields::hair_type},
    {"hair_color", &MiiFields::hair_color},
    {"hair_flip", &MiiFields::hair_flip},
    {"eye_type", &MiiFields::eye_type},
    {"eye_color", &MiiFields::eye_color},
    {"eye_scale", &MiiFields::eye_scale},
    {"eye_rotate", &MiiFields::eye_rotate},
    {"eye_x", &MiiFields::eye_x},
    {"eye_y", &MiiFields::eye_y},
    {"eyebrow_type", &MiiFields::eyebrow_type},
    {"eyebrow_color", &MiiFields::eyebrow_color},
    {"eyebrow_scale", &MiiFields::eyebrow_scale},
    {"eyebrow_rotate", &MiiFields::eyebrow_rotate},
    {"eyebrow_x", &MiiFields::eyebrow_x},
    {"eyebrow_y", &MiiFields::eyebrow_y},
    {"nose_type", &MiiFields::nose_type},
    {"nose_scale", &MiiFields::nose_scale},
    {"nose_y", &MiiFields::nose_y},
    {"mouth_type", &MiiFields::mouth_type},
    {"mouth_color", &MiiFields::mouth_color},
    {"mouth_scale", &MiiFields::mouth_scale},
    {"mouth_y", &MiiFields::mouth_y},
    {"mustache", &MiiFields::mustache},
    {"beard_type", &MiiFields::beard_type},
    {"beard_color", &MiiFields::beard_color},
    {"beard_scale", &MiiFields::beard_scale},
    {"beard_y", &MiiFields::beard_y},
    {"glass_type", &MiiFields::glass_type},
    {"glass_color", &MiiFields::glass_color},
    {"glass_scale", &MiiFields::glass_scale},
    {"glass_y", &MiiFields::glass_y},
    {"mole_type", &MiiFields::mole_type},
    {"mole_scale", &MiiFields::mole_scale},
    {"mole_x", &MiiFields::mole_x},
    {"mole_y", &MiiFields::mole_y},
};

constexpr char kHex[] = "0123456789abcdef";

template <typename T, std::size_t N>
std::string hex(const std::array<T, N>& values) {
    std::string out;
    for (const T v : values) {
        for (int shift = static_cast<int>(sizeof(T) * 8) - 4; shift >= 0; shift -= 4) {
            out.push_back(kHex[(v >> shift) & 0xFu]);
        }
    }
    return out;
}

template <typename T, std::size_t N>
bool parse_hex(std::string_view text, std::array<T, N>& values) {
    constexpr std::size_t digits = sizeof(T) * 2;
    if (text.size() != N * digits) {
        return false;
    }
    for (std::size_t i = 0; i < N; ++i) {
        unsigned value = 0;
        const char* first = text.data() + i * digits;
        const auto [end, error] = std::from_chars(first, first + digits, value, 16);
        if (error != std::errc{} || end != first + digits) {
            return false;
        }
        values[i] = static_cast<T>(value);
    }
    return true;
}

bool has_id(const MiiFields& mii) {
    return std::any_of(mii.create_id.begin(), mii.create_id.end(), [](std::uint8_t b) { return b != 0u; });
}

// A create ID a console keeps (not temporary), read from an imported record.
bool stored_id(const MiiRecord& record) {
    const auto id = std::span<const std::byte>(record).subspan(0x18, 8);
    return std::any_of(id.begin(), id.end(), [](std::byte b) { return b != std::byte{0}; }) &&
           (static_cast<std::uint8_t>(id[0]) & kTemporaryId) == 0u;
}

// The same Mii whatever its create ID (one exported twice by a tool that writes none) and favourite flag (one past
// the ten favourites was imported without it).
bool same_mii(MiiFields a, MiiFields b) {
    a.create_id = b.create_id = {};
    a.favorite = b.favorite = 0;
    return a == b;
}

void set_create_id(MiiFields& mii, std::uint32_t mii_part) {
    for (unsigned i = 0; i < 4u; ++i) {
        mii.create_id[i] = static_cast<std::uint8_t>(mii_part >> (24u - 8u * i));
        mii.create_id[4u + i] = static_cast<std::uint8_t>(kNebulaSystemId >> (24u - 8u * i));
    }
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
    std::filesystem::path file;
    std::filesystem::path console_database;
    std::filesystem::path folder;
    MiiPreparer prepare;
    std::vector<std::pair<unsigned, MiiFields>> installs;
    std::atomic<bool> pending{false};
    MiiLibrary miis;
    bool loaded = false;
    bool damaged = false;  // the file exists and is not a library: imports leave it alone
};

Library& library() {
    static Library instance;
    return instance;
}

// The library from its file; without one, from the console's database (its Miis in their slots, where saves that use
// them point), written as the file.
void load(Library& lib) {
    if (lib.loaded) {
        return;
    }
    lib.loaded = true;
    lib.damaged = false;
    lib.miis = {};
    if (lib.file.empty()) {
        return;
    }
    std::error_code error;
    if (std::filesystem::exists(lib.file, error)) {
        const std::vector<std::byte> bytes = read_file(lib.file);
        const std::optional<MiiLibrary> read =
            read_mii_library(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (read) {
            lib.miis = *read;
        } else {
            lib.damaged = true;
        }
        return;
    }
    if (lib.console_database.empty()) {
        return;
    }
    const auto console = read_wii_mii_database(read_file(lib.console_database));
    if (console.empty()) {
        return;
    }
    for (const auto& [slot, record] : console) {
        lib.miis.slots[slot] = decode_wii_mii(record);
    }
    (void)io::write_text_file_atomically(lib.file, write_mii_library(lib.miis));
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

std::string write_mii_library(const MiiLibrary& library) {
    std::string out(kLibraryFormat);
    out.push_back('\n');
    for (std::size_t slot = 0; slot < kMiiLibrarySlots; ++slot) {
        const std::optional<MiiFields>& mii = library.slots[slot];
        if (!mii) {
            continue;
        }
        out += std::to_string(slot);
        for (const NumberField& f : kNumbers) {
            out += ' ';
            out += f.name;
            out += '=';
            out += std::to_string((*mii).*f.member);
        }
        out += " name=" + hex(mii->name) + " creator=" + hex(mii->creator) + " create_id=" + hex(mii->create_id);
        out.push_back('\n');
    }
    return out;
}

std::optional<MiiLibrary> read_mii_library(std::string_view text) {
    const auto next_line = [&text]() {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        return line;
    };
    if (next_line() != kLibraryFormat) {
        return std::nullopt;
    }
    MiiLibrary library;
    while (!text.empty()) {
        std::string_view line = next_line();
        const std::size_t space = line.find(' ');
        unsigned slot = kMiiLibrarySlots;
        const std::string_view slot_text = line.substr(0, space);
        const auto [end, error] = std::from_chars(slot_text.data(), slot_text.data() + slot_text.size(), slot);
        if (error != std::errc{} || end != slot_text.data() + slot_text.size() || slot >= kMiiLibrarySlots ||
            space == std::string_view::npos) {
            continue;
        }
        MiiFields mii;
        line.remove_prefix(space + 1);
        while (!line.empty()) {
            const std::size_t gap = line.find(' ');
            const std::string_view item = line.substr(0, gap);
            line = gap == std::string_view::npos ? std::string_view{} : line.substr(gap + 1);
            const std::size_t eq = item.find('=');
            if (eq == std::string_view::npos) {
                continue;
            }
            const std::string_view name = item.substr(0, eq);
            const std::string_view value = item.substr(eq + 1);
            if (name == "name") {
                (void)parse_hex(value, mii.name);
            } else if (name == "creator") {
                (void)parse_hex(value, mii.creator);
            } else if (name == "create_id") {
                (void)parse_hex(value, mii.create_id);
            } else {
                for (const NumberField& f : kNumbers) {
                    unsigned number = 0;
                    if (f.name == name && std::from_chars(value.data(), value.data() + value.size(), number).ec ==
                                              std::errc{} && number <= 0xFFu) {
                        mii.*f.member = static_cast<std::uint8_t>(number);
                    }
                }
            }
        }
        if (has_id(mii)) {
            library.slots[slot] = mii;
        }
    }
    return library;
}

std::vector<MiiRecord> read_miis(std::span<const std::byte> bytes, std::string* error) {
    MiiFileContents contents = read_mii_file(bytes, {});
    if (error != nullptr) *error = contents.error;
    return std::move(contents.miis);
}

MiiMergeResult merge_miis(MiiLibrary& library, std::span<const MiiRecord> miis, const MiiPreparer& prepare,
                          std::vector<std::pair<unsigned, MiiFields>>* placed) {
    MiiMergeResult result;
    auto& slots = library.slots;
    // Fresh create IDs: the Wii's Mii ID (its type bits, then 4-second units since 2006-01-01) and this console's.
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    std::uint32_t next_time = static_cast<std::uint32_t>(
        ((std::chrono::duration_cast<std::chrono::seconds>(now).count() - 1136073600) / 4) & 0x1FFFFFFF);
    for (MiiRecord record : miis) {
        if (!stored_id(record) || (prepare && !prepare(record))) {
            ++result.invalid;
            continue;
        }
        const bool placeholder = mii_placeholder_id(record);
        MiiFields mii = decode_wii_mii(record);
        std::size_t id_slot = kMiiLibrarySlots;
        std::size_t free_slot = kMiiLibrarySlots;
        bool duplicate = false;
        unsigned favorites = 0u;
        for (std::size_t slot = 0; slot < kMiiLibrarySlots; ++slot) {
            if (!slots[slot]) {
                free_slot = std::min(free_slot, slot);
                continue;
            }
            favorites += slots[slot]->favorite != 0u ? 1u : 0u;
            duplicate = duplicate || same_mii(*slots[slot], mii);
            if (slots[slot]->create_id == mii.create_id) id_slot = slot;
        }
        if (duplicate) {
            ++result.duplicates;
            continue;
        }
        if (id_slot != kMiiLibrarySlots && !placeholder) {
            // The same Mii (a console's create ID), changed: over its old self, where saves that use it point.
            if (mii.favorite != 0u && slots[id_slot]->favorite == 0u && favorites >= kMiiFavoriteLimit) {
                mii.favorite = 0u;
                ++result.unfavorited;
            }
            slots[id_slot] = mii;
            ++result.updated;
            if (placed != nullptr) placed->emplace_back(static_cast<unsigned>(id_slot), mii);
            continue;
        }
        if (free_slot == kMiiLibrarySlots) {
            ++result.no_room;
            continue;
        }
        if (placeholder || id_slot != kMiiLibrarySlots) {
            // A create ID of its own: its type bits kept (special or not), unique in the library.
            const std::uint32_t type = static_cast<std::uint32_t>(mii.create_id[0] & 0xE0u) << 24u;
            for (;;) {
                set_create_id(mii, (type != 0u ? type : 0x80000000u) | (next_time++ & 0x1FFFFFFFu));
                const bool taken = std::any_of(slots.begin(), slots.end(), [&mii](const std::optional<MiiFields>& s) {
                    return s && s->create_id == mii.create_id;
                });
                if (!taken) break;
            }
        }
        if (mii.favorite != 0u && favorites >= kMiiFavoriteLimit) {
            mii.favorite = 0u;
            ++result.unfavorited;
        }
        slots[free_slot] = mii;
        ++result.added;
        if (placed != nullptr) placed->emplace_back(static_cast<unsigned>(free_slot), mii);
    }
    return result;
}

unsigned count_miis(const MiiLibrary& library) {
    return static_cast<unsigned>(std::count_if(library.slots.begin(), library.slots.end(),
                                               [](const std::optional<MiiFields>& s) { return s.has_value(); }));
}

void set_mii_library(const std::filesystem::path& file, const std::filesystem::path& console_database,
                     const std::filesystem::path& import_folder, MiiPreparer prepare) {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    lib.file = file;
    lib.console_database = console_database;
    lib.folder = import_folder;
    lib.prepare = std::move(prepare);
    lib.loaded = false;
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
    if (lib.file.empty()) {
        if (message != nullptr) *message = "NO MII LIBRARY";
        return total;
    }
    load(lib);
    if (lib.damaged) {
        if (message != nullptr) *message = "THE MII LIBRARY FILE IS DAMAGED";
        return total;
    }
    MiiLibrary miis = lib.miis;
    unsigned unreadable = 0u;
    unsigned unkept = 0u;
    std::vector<std::pair<unsigned, MiiFields>> placed;
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
        const MiiMergeResult r = merge_miis(miis, contents.miis, lib.prepare, &placed);
        total.added += r.added;
        total.updated += r.updated;
        total.duplicates += r.duplicates;
        total.invalid += r.invalid;
        total.no_room += r.no_room;
        total.unfavorited += r.unfavorited;
        total.converted += contents.converted;
    }
    if (!placed.empty()) {
        if (!io::write_text_file_atomically(lib.file, write_mii_library(miis))) {
            if (message != nullptr) *message = "COULD NOT WRITE THE MII LIBRARY";
            return MiiMergeResult{};
        }
        lib.miis = miis;
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
    load(lib);
    return count_miis(lib.miis);
}

std::optional<MiiFields> mii_library_slot(unsigned slot) {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    load(lib);
    if (slot >= kMiiLibrarySlots) {
        return std::nullopt;
    }
    return lib.miis.slots[slot];
}

std::vector<std::pair<unsigned, MiiFields>> take_mii_installs() {
    Library& lib = library();
    if (!lib.pending.load(std::memory_order_acquire)) return {};
    const std::lock_guard<std::mutex> lock(lib.mutex);
    lib.pending.store(false, std::memory_order_relaxed);
    return std::exchange(lib.installs, {});
}

}  // namespace galaxy::host
