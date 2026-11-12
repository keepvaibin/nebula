// SPDX-License-Identifier: GPL-3.0-only
// Mod import (mod_import.h): libarchive (third_party/archive) reads the archives.

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#endif

#include "galaxy/mod_import.h"

#include "galaxy/luma.h"
#include "galaxy/mods.h"
#include "galaxy/dolphin_pack.h"
#include "galaxy/texture_categories.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <system_error>

#if defined(_WIN32)
#include <shellapi.h>
#else
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#endif

namespace galaxy::mods {
namespace fs = std::filesystem;

namespace {

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

fs::path from_utf8(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// A folder name from a file or folder name: no characters a file system refuses, no trailing dots or spaces.
std::string folder_name(std::string name) {
    for (const char* suffix : {".tar"}) {
        if (lower(name).ends_with(suffix)) name.resize(name.size() - std::strlen(suffix));
    }
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(u < 32u || std::strchr("<>:\"/\\|?*", c) != nullptr ? '_' : c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());
    if (out.empty()) return "Mod";
    // A name the system keeps for a device (CON, NUL.arc...) can't be a folder.
    return safe_path_part(out) ? out : "_" + out;
}

// Files a system leaves in folders and archives, never part of a mod.
bool system_clutter(const std::string& name) {
    const std::string n = lower(name);
    return n == "thumbs.db" || n == "desktop.ini" || n == ".ds_store" || n == "__macosx" || n.starts_with("._");
}

// Past these an archive is refused: counted as extracted, since a header's sizes can lie. Texture packs reach tens of
// gigabytes and a hundred thousand files.
constexpr std::uint64_t kImportByteLimit = std::uint64_t{256} << 30u;
constexpr std::uint64_t kImportEntryLimit = 1000000u;

// An entry's path inside the staging folder; empty when it would leave it (absolute, a drive, `..`) or is clutter.
fs::path safe_relative(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.front() == '/') return {};
    fs::path out;
    std::size_t at = 0;
    while (at <= name.size()) {
        const std::size_t slash = std::min(name.find('/', at), name.size());
        const std::string part = name.substr(at, slash - at);
        at = slash + 1u;
        if (part.empty() || part == ".") continue;
        if (!safe_path_part(part) || system_clutter(part)) return {};
        out /= from_utf8(part);
    }
    return out;
}

unsigned long long process_id() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long long>(getpid());
#endif
}

// Whether the run with that process id is still going (its import may be too).
bool process_running(unsigned long long id) {
#if defined(_WIN32)
    if (id > 0xFFFFFFFFull) return false;
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(id));
    if (process == nullptr) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
#else
    if (id == 0u || id > static_cast<unsigned long long>(std::numeric_limits<pid_t>::max())) return false;
    return kill(static_cast<pid_t>(id), 0) == 0 || errno == EPERM;
#endif
}

fs::path staging_folder(const fs::path& mods_folder, const std::string& id) {
    static std::atomic<unsigned> counter{0};
    return mods_folder / from_utf8(".importing-" + id + "-" + std::to_string(process_id()) + "-" +
                                   std::to_string(counter.fetch_add(1u)));
}

// Where an update waits while its mod's folder is in use.
fs::path pending_folder(const fs::path& mods_folder, const std::string& id) {
    return mods_folder / from_utf8(".update-" + id);
}

// Notes that may sit beside an archive's one top folder (it is still the mod's folder).
bool note_file(const fs::path& file) {
    const std::string extension = lower(utf8(file.extension()));
    for (const char* known : {".txt", ".md", ".pdf", ".url", ".htm", ".html", ".rtf", ".png", ".jpg", ".jpeg",
                              ".webp", ".gif", ".nfo"}) {
        if (extension == known) return true;
    }
    return false;
}

// An archive's single top folder (with only notes beside it) becomes the mod's folder; twice at most.
void lift_top_folder(const fs::path& staging) {
    for (int pass = 0; pass < 2; ++pass) {
        std::error_code error;
        std::error_code walk_error;
        fs::path only;
        std::vector<fs::path> notes;
        bool lift = true;
        for (auto it = fs::directory_iterator(staging, walk_error); !walk_error && it != fs::directory_iterator();
             it.increment(walk_error)) {
            if (it->is_directory(error)) {
                if (!only.empty()) {
                    lift = false;
                    break;
                }
                only = it->path();
            } else if (note_file(it->path())) {
                notes.push_back(it->path());
            } else {
                lift = false;
                break;
            }
        }
        // An unread folder is not known to hold one folder only.
        if (walk_error || !lift || only.empty()) return;
        // The folder's contents up; a note whose name the folder also has stays the folder's.
        const fs::path moved = staging / from_utf8(".lifting");
        fs::rename(only, moved, error);
        if (error) return;
        walk_error.clear();
        for (auto it = fs::directory_iterator(moved, walk_error); !walk_error && it != fs::directory_iterator();
             it.increment(walk_error)) {
            const fs::path target = staging / it->path().filename();
            if (fs::exists(target, error)) fs::remove_all(target, error);
            fs::rename(it->path(), target, error);
        }
        fs::remove_all(moved, error);
    }
}

// A file's path inside its mod folder, '/'-separated UTF-8.
std::string relative_key(const fs::path& file, const fs::path& root) {
    const std::u8string text = file.lexically_relative(root).generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// Every file of a mod folder by its path inside it, with its size; clutter left out.
std::map<std::string, std::uint64_t> file_sizes(const fs::path& root) {
    std::map<std::string, std::uint64_t> out;
    std::error_code error;
    fs::recursive_directory_iterator it(root, error);
    for (const fs::recursive_directory_iterator end{}; !error && it != end; it.increment(error)) {
        if (system_clutter(utf8(it->path().filename()))) {
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error)) out[relative_key(it->path(), root)] = it->file_size(error);
    }
    return out;
}

bool same_bytes(const fs::path& a, const fs::path& b, std::vector<char>& one, std::vector<char>& two) {
    std::ifstream first(a, std::ios::binary);
    std::ifstream second(b, std::ios::binary);
    if (!first || !second) return false;
    while (first && second) {
        first.read(one.data(), static_cast<std::streamsize>(one.size()));
        second.read(two.data(), static_cast<std::streamsize>(two.size()));
        if (first.gcount() != second.gcount() ||
            std::memcmp(one.data(), two.data(), static_cast<std::size_t>(first.gcount())) != 0) {
            return false;
        }
    }
    return first.eof() && second.eof();
}

// Whether the mod folder `other` holds exactly the files staged (same paths, sizes and bytes; clutter aside). Most
// folders differ at their first file, so a mods folder of large packs is not read through.
bool same_mod(const fs::path& staging, const std::map<std::string, std::uint64_t>& files, const fs::path& other,
              std::vector<char>& one, std::vector<char>& two) {
    std::error_code error;
    std::size_t seen = 0;
    fs::recursive_directory_iterator it(other, error);
    for (const fs::recursive_directory_iterator end{}; !error && it != end; it.increment(error)) {
        if (system_clutter(utf8(it->path().filename()))) {
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(error)) continue;
        if (!it->is_regular_file(error)) return false;
        const auto found = files.find(relative_key(it->path(), other));
        if (found == files.end() || found->second != it->file_size(error)) return false;
        ++seen;
    }
    if (error || seen != files.size()) return false;
    for (const auto& [name, size] : files) {
        if (size != 0u && !same_bytes(staging / from_utf8(name), other / from_utf8(name), one, two)) return false;
    }
    return true;
}

// The mod already in `mods_folder` with exactly the staged files (`id`'s own folder first), or empty.
std::string identical_mod(const fs::path& staging, const fs::path& mods_folder, const std::string& id) {
    const std::map<std::string, std::uint64_t> files = file_sizes(staging);
    if (files.empty()) return {};
    std::vector<char> one(std::size_t{1} << 20u);
    std::vector<char> two(one.size());
    std::error_code error;
    if (fs::is_directory(mods_folder / from_utf8(id), error) && same_mod(staging, files, mods_folder / from_utf8(id), one, two)) {
        return id;
    }
    std::error_code walk_error;
    for (auto it = fs::directory_iterator(mods_folder, walk_error); !walk_error && it != fs::directory_iterator();
         it.increment(walk_error)) {
        const std::string name = utf8(it->path().filename());
        if (name.empty() || name.front() == '.' || name == id || !it->is_directory(error)) continue;
        if (same_mod(staging, files, it->path(), one, two)) return name;
    }
    return {};
}

// Its name and what it holds, as the MODS page lists them.
void describe(ImportResult& result) {
    result.title = result.id;
    for (const ModInfo& mod : scan_mods()) {
        if (mod.id != result.id) continue;
        if (!mod.pack_title.empty()) result.title = mod.pack_title;
        result.kinds = change_kinds(mod);
        // As the menu translates it: the words before ": ", the name, then the line of its own.
        if (!mod.problem.empty() && !result.already) result.message = "IMPORTED: " + result.title + ". " + mod.problem;
    }
}

struct Writer {
    fs::path staging;
    const std::function<void(const ImportProgress&)>* progress = nullptr;
    const std::atomic<bool>* cancel = nullptr;
    ImportProgress state;
    std::chrono::steady_clock::time_point last_report{};

    void report(bool force = false) {
        if (progress == nullptr || !*progress) return;
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - last_report < std::chrono::milliseconds(100)) return;
        last_report = now;
        (*progress)(state);
    }
    [[nodiscard]] bool cancelled() const { return cancel != nullptr && cancel->load(std::memory_order_relaxed); }
};

// One archive into `writer.staging`; false with `message` on a failure.
bool extract_archive(const fs::path& source, Writer& writer, ImportResult& result) {
    archive* reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    struct Close {
        archive* a;
        ~Close() { archive_read_free(a); }
    } close{reader};
#if defined(_WIN32)
    const int opened = archive_read_open_filename_w(reader, source.c_str(), 1u << 20);
#else
    const int opened = archive_read_open_filename(reader, source.c_str(), 1u << 20);
#endif
    if (opened != ARCHIVE_OK) {
        result.message = std::string("CANNOT OPEN THE ARCHIVE: ") + (archive_error_string(reader) ? archive_error_string(reader) : "");
        return false;
    }
    std::error_code error;
    writer.state.total = fs::file_size(source, error);
    std::vector<char> buffer(1u << 20);
    archive_entry* entry = nullptr;
    std::uint64_t entries = 0;
    for (;;) {
        const int next = archive_read_next_header(reader, &entry);
        if (next == ARCHIVE_EOF) break;
        if (next == ARCHIVE_FATAL || next == ARCHIVE_FAILED) {
            result.message = "THIS DOWNLOAD IS DAMAGED. DOWNLOAD IT AGAIN";
            return false;
        }
        if (++entries > kImportEntryLimit) {
            result.message = "THE ARCHIVE HOLDS TOO MANY FILES";
            return false;
        }
        if (writer.cancelled()) {
            result.message = "CANCELLED";
            return false;
        }
        if (archive_entry_is_encrypted(entry)) {
            result.message = "THIS ARCHIVE HAS A PASSWORD. UNPACK IT FIRST, THEN ADD THE FOLDER";
            return false;
        }
        std::string name;
#if defined(_WIN32)
        if (const wchar_t* wide = archive_entry_pathname_w(entry)) {
            name = utf8(fs::path(wide));
        } else if (const char* narrow = archive_entry_pathname(entry)) {
            name = narrow;
        }
#else
        if (const char* text = archive_entry_pathname_utf8(entry)) {
            name = text;
        } else if (const char* narrow = archive_entry_pathname(entry)) {
            name = narrow;
        }
#endif
        const fs::path relative = safe_relative(name);
        const auto type = archive_entry_filetype(entry);
        if (relative.empty() || archive_entry_hardlink(entry) != nullptr || type == AE_IFLNK ||
            (type != AE_IFREG && type != AE_IFDIR)) {
            continue;  // unsafe, clutter or a link: not extracted
        }
        const fs::path target = writer.staging / relative;
        if (type == AE_IFDIR) {
            fs::create_directories(target, error);
            continue;
        }
        fs::create_directories(target.parent_path(), error);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.message = "CANNOT WRITE: " + utf8(relative.filename()) + ". IS THE DISK FULL?";
            return false;
        }
        writer.state.current = utf8(relative);
        for (;;) {
            const la_ssize_t read = archive_read_data(reader, buffer.data(), buffer.size());
            if (read == 0) break;
            if (read < 0) {
                const char* why = archive_error_string(reader);
                result.message = "CANNOT READ: " + utf8(relative.filename()) + (why != nullptr && why[0] != '\0' ? " (" + std::string(why) + ")" : std::string());
                return false;
            }
            out.write(buffer.data(), read);
            if (!out) {
                result.message = "CANNOT WRITE: " + utf8(relative.filename()) + ". IS THE DISK FULL?";
                return false;
            }
            result.bytes += static_cast<std::uint64_t>(read);
            if (result.bytes > kImportByteLimit) {
                result.message = "THE ARCHIVE UNPACKS TO TOO MUCH DATA";
                return false;
            }
            writer.state.done = static_cast<std::uint64_t>(archive_filter_bytes(reader, -1));
            writer.report();
            if (writer.cancelled()) {
                result.message = "CANCELLED";
                return false;
            }
        }
        ++result.files;
    }
    return true;
}

bool copy_folder(const fs::path& source, Writer& writer, ImportResult& result) {
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (it->is_regular_file(error)) writer.state.total += it->file_size(error);
    }
    error.clear();
    for (auto it = fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (system_clutter(utf8(it->path().filename()))) {
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink(error)) continue;
        const fs::path relative = fs::relative(it->path(), source, error);
        const fs::path target = writer.staging / relative;
        if (it->is_directory(error)) {
            fs::create_directories(target, error);
            continue;
        }
        if (!it->is_regular_file(error)) continue;
        fs::create_directories(target.parent_path(), error);
        writer.state.current = utf8(relative);
        std::error_code copy_error;
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, copy_error);
        if (copy_error) {
            result.message = "CANNOT COPY: " + utf8(relative.filename()) + " (" + copy_error.message() + ")";
            return false;
        }
        const std::uint64_t size = it->file_size(error);
        result.bytes += size;
        ++result.files;
        writer.state.done += size;
        writer.report();
        if (writer.cancelled()) {
            result.message = "CANCELLED";
            return false;
        }
    }
    if (error) {
        result.message = "CANNOT READ THE FOLDER: " + error.message();
        return false;
    }
    return true;
}

}  // namespace

bool is_mod_archive(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return false;
    archive* reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
#if defined(_WIN32)
    bool ok = archive_read_open_filename_w(reader, path.c_str(), 1u << 16) == ARCHIVE_OK;
#else
    bool ok = archive_read_open_filename(reader, path.c_str(), 1u << 16) == ARCHIVE_OK;
#endif
    archive_entry* entry = nullptr;
    // A plain file opens as libarchive's "raw" or "empty" format: an archive gives a header of a known format.
    ok = ok && archive_read_next_header(reader, &entry) == ARCHIVE_OK && archive_format(reader) != ARCHIVE_FORMAT_RAW &&
         archive_format(reader) != ARCHIVE_FORMAT_EMPTY;
    archive_read_free(reader);
    return ok;
}

ImportResult import_mod(const fs::path& source, const fs::path& mods_folder,
                        const std::function<void(const ImportProgress&)>& progress, const std::atomic<bool>* cancel,
                        const ImportCheck& check) {
    ImportResult result;
    std::error_code error;
    const bool folder = fs::is_directory(source, error);
    if (!folder && !fs::is_regular_file(source, error)) {
        result.message = "NOT FOUND: " + utf8(source.filename());
        return result;
    }
    if (!folder && !is_mod_archive(source)) {
        result.message = "NOT AN ARCHIVE NEBULA READS (ZIP, 7Z, RAR OR TAR): " + utf8(source.filename());
        return result;
    }
    if (folder) {
        // A mod folder inside the mods folder (or the mods folder itself) is already there.
        const fs::path canonical_source = fs::weakly_canonical(source, error);
        const fs::path canonical_mods = fs::weakly_canonical(mods_folder, error);
        if (canonical_source == canonical_mods || canonical_source.parent_path() == canonical_mods) {
            result.message = "THIS FOLDER IS ALREADY IN THE MODS FOLDER";
            return result;
        }
    }
    fs::create_directories(mods_folder, error);
    result.id = folder_name(utf8(folder ? source.filename() : source.stem()));
    Writer writer;
    writer.staging = staging_folder(mods_folder, result.id);
    writer.progress = &progress;
    writer.cancel = cancel;
    fs::create_directories(writer.staging, error);
    if (error) {
        result.message = "CANNOT WRITE TO THE MODS FOLDER: " + error.message();
        return result;
    }
    const bool copied = folder ? copy_folder(source, writer, result) : extract_archive(source, writer, result);
    if (!copied || result.files == 0u) {
        if (copied) result.message = folder ? "THE FOLDER HOLDS NO FILES" : "THE ARCHIVE HOLDS NO FILES";
        fs::remove_all(writer.staging, error);
        result.ok = false;
        return result;
    }
    if (!folder) lift_top_folder(writer.staging);
    const std::optional<galaxy::luma::Manifest> manifest = galaxy::luma::read_manifest(writer.staging);
    if (manifest && !manifest->id.empty()) {
        // A .luma is its id: the folder holding that id is the one updated, whatever its name (copied in by hand, or
        // renamed). A folder of its name holding another mod is left alone: this one takes a name of its own.
        result.new_version = manifest->version;
        const auto id_in = [&](const fs::path& folder) {
            const std::optional<galaxy::luma::Manifest> there = galaxy::luma::read_manifest(folder);
            return there && there->id == manifest->id ? std::optional<std::string>(there->version) : std::nullopt;
        };
        result.id = folder_name(manifest->id);
        std::error_code walk_error;
        std::optional<std::string> found;
        if (fs::is_directory(mods_folder / from_utf8(result.id), error)) found = id_in(mods_folder / from_utf8(result.id));
        for (auto it = fs::directory_iterator(mods_folder, walk_error); !found && !walk_error && it != fs::directory_iterator();
             it.increment(walk_error)) {
            const std::string name = utf8(it->path().filename());
            if (name.empty() || name.front() == '.' || !it->is_directory(error)) continue;
            if ((found = id_in(it->path()))) result.id = name;
        }
        if (found) {
            result.old_version = *found;
        } else {
            for (unsigned n = 2u; fs::exists(mods_folder / from_utf8(result.id), error); ++n) {
                result.id = folder_name(manifest->id) + "-" + std::to_string(n);
            }
        }
    }
    // A Dolphin texture pack takes the game's texture names here, once (dolphin_pack.h); compared and kept converted.
    if (galaxy::textures::has_dolphin_files(writer.staging) && galaxy::textures::wait_for_index()) {
        const galaxy::textures::DolphinConversion converted =
            galaxy::textures::convert_dolphin_pack(writer.staging, galaxy::textures::names());
        result.dolphin_files = converted.files;
        result.dolphin_textures = converted.textures;
        result.dolphin_unmatched = converted.unmatched.size();
    }
    // The same pack again (under any file name, as a browser's "(1)" copy, or the folder it came from): nothing is
    // added or replaced, and the player is told where it is.
    if (const std::string same = identical_mod(writer.staging, mods_folder, result.id); !same.empty()) {
        fs::remove_all(writer.staging, error);
        result.id = same;
        result.already = true;
        result.ok = true;
        describe(result);
        result.message = "ALREADY IN YOUR MODS: " + result.title;
        return result;
    }
    // A package passes the check before anything is placed (validator 2 "at import"): refused, nothing is added.
    if (manifest && check && !check(writer.staging, result.id, result) && result.refused) {
        fs::remove_all(writer.staging, error);
        result.ok = false;
        const std::string name = manifest->name.fallback.empty() ? manifest->id : manifest->name.fallback;
        result.title = name;
        result.message = "NOT IMPORTED: " + name;  // the result page lists the findings under it
        return result;
    }
    // In place: an existing mod of that name is replaced (its settings, kept by name, stay).
    const fs::path final_folder = mods_folder / from_utf8(result.id);
    fs::path previous;
    if (fs::exists(final_folder, error)) {
        previous = staging_folder(mods_folder, result.id + "-previous");
        fs::rename(final_folder, previous, error);
        if (error) {
            // In use (Windows: a program has a file of it open): the update waits beside it for the next start.
            const fs::path waiting = pending_folder(mods_folder, result.id);
            std::error_code pending_error;
            fs::remove_all(waiting, pending_error);
            fs::rename(writer.staging, waiting, pending_error);
            if (pending_error) {
                fs::remove_all(writer.staging, error);
                result.message = "IN USE: " + result.id + ". CLOSE WHAT HAS ITS FILES OPEN AND TRY AGAIN";
                return result;
            }
            previous.clear();
            error.clear();
            result.pending = true;
        }
        result.replaced = true;
    }
    if (!result.pending) fs::rename(writer.staging, final_folder, error);
    if (error) {
        if (!previous.empty()) fs::rename(previous, final_folder, error);
        fs::remove_all(writer.staging, error);
        result.message = "CANNOT MOVE THE MOD INTO PLACE";
        result.replaced = false;
        return result;
    }
    if (!previous.empty()) fs::remove_all(previous, error);
    // An update that waited from before is older than this one.
    if (!result.pending) fs::remove_all(pending_folder(mods_folder, result.id), error);
    writer.state.done = writer.state.total;
    writer.report(true);
    describe(result);
    result.ok = true;
    if (result.message.empty()) {
        const std::string name = result.title.empty() ? result.id : result.title;
        if (result.replaced && !result.old_version.empty() && !result.new_version.empty()) {
            const int order = galaxy::luma::compare_versions(result.new_version, result.old_version);
            // As the menu translates it: the words before ": ", the name, the versions in brackets.
            result.message = order > 0   ? "UPDATED: " + name + " (" + result.old_version + " TO " + result.new_version + ")"
                             : order < 0 ? "OLDER VERSION: " + name + " (" + result.old_version + " TO " + result.new_version + ")"
                                         : "REPLACED: " + name + " (" + result.new_version + ")";
        } else {
            result.message = (result.replaced ? "UPDATED: " : "IMPORTED: ") + name +
                             (result.kinds.empty() ? std::string() : " (" + result.kinds + ")");
        }
    }
    return result;
}

bool remove_mod(const fs::path& mods_folder, const std::string& id, std::string* message) {
    // A folder in the mods folder by its name: nothing else (no path, no folder Nebula keeps there).
    std::error_code error;
    if (id.empty() || id.front() == '.' || id.find_first_of("/\\:") != std::string::npos ||
        !fs::is_directory(mods_folder / from_utf8(id), error)) {
        if (message != nullptr) *message = "NO MOD: " + id;
        return false;
    }
    const fs::path folder = mods_folder / from_utf8(id);
#if defined(_WIN32)
    // To the recycle bin, so it can be restored.
    std::wstring from = folder.wstring();
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted && !fs::exists(folder, error)) {
        if (message != nullptr) *message = "MOVED TO THE RECYCLE BIN: " + id;
        return true;
    }
#endif
    // Moved aside first, so a folder whose files are open is left whole rather than half removed.
    const fs::path aside = staging_folder(mods_folder, id + "-removed");
    fs::rename(folder, aside, error);
    if (error) {
        if (message != nullptr) *message = "IN USE: " + id + ". CLOSE WHAT HAS ITS FILES OPEN AND TRY AGAIN";
        return false;
    }
    fs::remove_all(pending_folder(mods_folder, id), error);
    fs::remove_all(aside, error);
    if (message != nullptr) *message = "REMOVED: " + id;
    return true;
}

void apply_pending_updates(const fs::path& mods_folder) {
    std::error_code error;
    std::vector<std::pair<fs::path, std::string>> waiting;
    // What an import or removal that was cut off (the game closed or crashed during it) left, by another run: its
    // staging folders (".importing-<id>-<run>-<n>"). A mod moved aside for its update ("<id>-previous") goes back when
    // the update never took its place; the rest is removed.
    std::vector<fs::path> leftovers;
    std::vector<std::pair<fs::path, std::string>> set_aside;
    for (auto it = fs::directory_iterator(mods_folder, error); !error && it != fs::directory_iterator(); it.increment(error)) {
        const std::string name = utf8(it->path().filename());
        std::error_code type_error;
        if (name.starts_with(".update-") && name.size() > 8u && it->is_directory(type_error)) {
            waiting.emplace_back(it->path(), name.substr(8u));
            continue;
        }
        if (!name.starts_with(".importing-")) continue;
        const std::size_t counter = name.rfind('-');
        const std::size_t run = counter == std::string::npos || counter == 0u ? std::string::npos : name.rfind('-', counter - 1u);
        if (run == std::string::npos || run < 11u) continue;
        const std::string run_text = name.substr(run + 1u, counter - run - 1u);
        if (run_text.empty() || run_text.size() > 19u || run_text.find_first_not_of("0123456789") != std::string::npos ||
            process_running(std::stoull(run_text))) {
            continue;  // not a staging folder's name, or a run still going (this one's own import too)
        }
        const std::string what = name.substr(11u, run - 11u);
        if (what.ends_with("-previous") && what.size() > 9u) {
            set_aside.emplace_back(it->path(), what.substr(0u, what.size() - 9u));
        } else {
            leftovers.push_back(it->path());
        }
    }
    for (const auto& [aside, id] : set_aside) {
        std::error_code move_error;
        if (!fs::exists(mods_folder / from_utf8(id), move_error) && !move_error) {
            fs::rename(aside, mods_folder / from_utf8(id), move_error);
            if (!move_error) continue;
        }
        leftovers.push_back(aside);
    }
    for (const fs::path& leftover : leftovers) {
        std::error_code remove_error;
        fs::remove_all(leftover, remove_error);
    }
    for (const auto& [update, id] : waiting) {
        const fs::path folder = mods_folder / from_utf8(id);
        const fs::path previous = staging_folder(mods_folder, id + "-previous");
        std::error_code move_error;
        if (fs::exists(folder, move_error)) {
            fs::rename(folder, previous, move_error);
            if (move_error) continue;  // still in use: the next start
        }
        fs::rename(update, folder, move_error);
        if (move_error) {
            fs::rename(previous, folder, move_error);
            continue;
        }
        fs::remove_all(previous, move_error);
    }
}

}  // namespace galaxy::mods
