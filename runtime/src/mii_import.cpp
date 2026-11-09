// SPDX-License-Identifier: GPL-3.0-only
// The Mii library and Mii import (mii_import.h).

#include "galaxy/mii_import.h"
#include "galaxy/path_text.h"

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
#include <utility>

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

// The same Mii whatever its create ID (one exported twice by a tool that writes none) and favourite flag (a choice
// about the Mii, not the Mii).
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

// The folder is read every start; a file past this holds no Miis (the largest database is 2 MB). Reads as empty.
constexpr std::uintmax_t kMiiFileLimit = 64u << 20u;

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > kMiiFileLimit) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    return bytes;
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string ext = galaxy::path_text(path.extension());
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
    MiiCollection miis;
    bool loaded = false;
    bool damaged = false;  // the file exists and is not a library: imports leave it alone
};

Library& library() {
    static Library instance;
    return instance;
}

// The library from its file; without one, from the save folder's Wii Mii database (its Miis in its order), written as
// the file.
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
        std::optional<MiiCollection> read =
            read_mii_library(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (read) {
            lib.miis = std::move(*read);
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
    lib.miis.miis.reserve(console.size());
    for (const MiiRecord& record : console) {
        lib.miis.miis.push_back(decode_wii_mii(record));
    }
    (void)io::write_file_atomically(lib.file, write_mii_library(lib.miis));
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
            return io::write_file_atomically(
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
    if (r.converted != 0u) text += ", " + std::to_string(r.converted) + " FROM NEWER CONSOLES";
    return text;
}

}  // namespace

std::string write_mii_library(const MiiCollection& library) {
    std::string out(kLibraryFormat);
    out.push_back('\n');
    for (std::size_t place = 0; place < library.miis.size(); ++place) {
        const MiiFields& mii = library.miis[place];
        out += std::to_string(place);
        for (const NumberField& f : kNumbers) {
            out += ' ';
            out += f.name;
            out += '=';
            out += std::to_string(mii.*f.member);
        }
        out += " name=" + hex(mii.name) + " creator=" + hex(mii.creator) + " create_id=" + hex(mii.create_id);
        out.push_back('\n');
    }
    return out;
}

std::optional<MiiCollection> read_mii_library(std::string_view text) {
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
    // Each line's place and Mii; the places only order them (a place written twice keeps its later line).
    std::vector<std::pair<std::uint64_t, MiiFields>> placed;
    while (!text.empty()) {
        std::string_view line = next_line();
        const std::size_t space = line.find(' ');
        std::uint64_t place = 0;
        const std::string_view place_text = line.substr(0, space);
        const auto [end, error] = std::from_chars(place_text.data(), place_text.data() + place_text.size(), place);
        if (error != std::errc{} || end != place_text.data() + place_text.size() || space == std::string_view::npos) {
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
            placed.emplace_back(place, mii);
        }
    }
    std::stable_sort(placed.begin(), placed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    MiiCollection library;
    library.miis.reserve(placed.size());
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (i + 1u < placed.size() && placed[i + 1u].first == placed[i].first) {
            continue;
        }
        library.miis.push_back(placed[i].second);
    }
    return library;
}

std::vector<MiiRecord> read_miis(std::span<const std::byte> bytes, std::string* error) {
    MiiFileContents contents = read_mii_file(bytes, {});
    if (error != nullptr) *error = contents.error;
    return std::move(contents.miis);
}

MiiMergeResult merge_miis(MiiCollection& library, std::span<const MiiRecord> miis, const MiiPreparer& prepare,
                          std::vector<std::pair<unsigned, MiiFields>>* placed) {
    MiiMergeResult result;
    std::vector<MiiFields>& held = library.miis;
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
        std::size_t id_at = held.size();
        bool duplicate = false;
        for (std::size_t at = 0; at < held.size() && !duplicate; ++at) {
            duplicate = same_mii(held[at], mii);
            if (held[at].create_id == mii.create_id) id_at = at;
        }
        if (duplicate) {
            ++result.duplicates;
            continue;
        }
        if (id_at != held.size() && !placeholder) {
            // The same Mii (a console's create ID), changed: over its old self, where saves that use it point.
            held[id_at] = mii;
            ++result.updated;
            if (placed != nullptr) placed->emplace_back(static_cast<unsigned>(id_at), mii);
            continue;
        }
        if (placeholder || id_at != held.size()) {
            // A create ID of its own: its type bits kept (special or not), unique in the library.
            const std::uint32_t type = static_cast<std::uint32_t>(mii.create_id[0] & 0xE0u) << 24u;
            for (;;) {
                set_create_id(mii, (type != 0u ? type : 0x80000000u) | (next_time++ & 0x1FFFFFFFu));
                const bool taken = std::any_of(held.begin(), held.end(),
                                               [&mii](const MiiFields& m) { return m.create_id == mii.create_id; });
                if (!taken) break;
            }
        }
        held.push_back(mii);
        ++result.added;
        if (placed != nullptr) placed->emplace_back(static_cast<unsigned>(held.size() - 1u), mii);
    }
    return result;
}

unsigned count_miis(const MiiCollection& library) {
    return static_cast<unsigned>(library.miis.size());
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
    MiiCollection miis = lib.miis;
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
        total.converted += contents.converted;
    }
    if (!placed.empty()) {
        if (!io::write_file_atomically(lib.file, write_mii_library(miis))) {
            if (message != nullptr) *message = "COULD NOT WRITE THE MII LIBRARY";
            return MiiMergeResult{};
        }
        lib.miis = std::move(miis);
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
        // increment(ec) ends the list when the folder goes away mid-walk; the range-for form throws there.
        std::error_code walk_error;
        for (auto it = std::filesystem::directory_iterator(folder, walk_error);
             !walk_error && it != std::filesystem::directory_iterator(); it.increment(walk_error)) {
            if (it->is_regular_file(error)) files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        if (message != nullptr) *message = "NO FILES";
        return {};
    }
    return import_mii_files(files, message);
}

MiiFolderAdd add_to_mii_folder(const std::filesystem::path& folder, const std::filesystem::path& name,
                               std::span<const std::byte> bytes) {
    MiiFolderAdd added;
    const std::filesystem::path file = name.filename();
    const MiiFileContents contents = read_mii_file(bytes, lower_extension(file));
    added.miis = static_cast<unsigned>(contents.miis.size());
    added.converted = contents.converted;
    added.error = contents.error;
    if (added.miis != 0u && !file.empty()) {
        added.kept = keep_in_library(folder, file, bytes);
    }
    return added;
}

std::optional<unsigned> mii_library_file_count(const std::filesystem::path& file) {
    std::error_code error;
    if (file.empty() || !std::filesystem::is_regular_file(file, error)) {
        return std::nullopt;
    }
    const std::vector<std::byte> bytes = read_file(file);
    const std::optional<MiiCollection> read =
        read_mii_library(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!read) {
        return std::nullopt;
    }
    return count_miis(*read);
}

unsigned mii_library_count() {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    load(lib);
    return count_miis(lib.miis);
}

std::optional<MiiFields> mii_library_mii(unsigned place) {
    Library& lib = library();
    const std::lock_guard<std::mutex> lock(lib.mutex);
    load(lib);
    if (place >= lib.miis.miis.size()) {
        return std::nullopt;
    }
    return lib.miis.miis[place];
}

std::vector<std::pair<unsigned, MiiFields>> take_mii_installs() {
    Library& lib = library();
    if (!lib.pending.load(std::memory_order_acquire)) return {};
    const std::lock_guard<std::mutex> lock(lib.mutex);
    lib.pending.store(false, std::memory_order_relaxed);
    return std::exchange(lib.installs, {});
}

}  // namespace galaxy::host
