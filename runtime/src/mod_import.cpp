// SPDX-License-Identifier: GPL-3.0-only
// Mod import (mod_import.h): libarchive (third_party/archive) reads the archives.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "galaxy/mod_import.h"

#include "galaxy/mods.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
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
    return out.empty() ? std::string("Mod") : out;
}

// Files a system leaves in folders and archives, never part of a mod.
bool system_clutter(const std::string& name) {
    const std::string n = lower(name);
    return n == "thumbs.db" || n == "desktop.ini" || n == ".ds_store" || n == "__macosx" || n.starts_with("._");
}

// An entry's path inside the staging folder; empty when it would leave it (absolute, a drive, `..`) or is clutter.
fs::path safe_relative(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.front() == '/' || (name.size() >= 2u && name[1] == ':')) return {};
    fs::path out;
    std::size_t at = 0;
    while (at <= name.size()) {
        const std::size_t slash = std::min(name.find('/', at), name.size());
        const std::string part = name.substr(at, slash - at);
        at = slash + 1u;
        if (part.empty() || part == ".") continue;
        if (part == ".." || system_clutter(part)) return {};
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

fs::path staging_folder(const fs::path& mods_folder, const std::string& id) {
    static std::atomic<unsigned> counter{0};
    return mods_folder / from_utf8(".importing-" + id + "-" + std::to_string(process_id()) + "-" +
                                   std::to_string(counter.fetch_add(1u)));
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
        fs::path only;
        std::vector<fs::path> notes;
        bool lift = true;
        for (const auto& entry : fs::directory_iterator(staging, error)) {
            if (entry.is_directory(error)) {
                if (!only.empty()) {
                    lift = false;
                    break;
                }
                only = entry.path();
            } else if (note_file(entry.path())) {
                notes.push_back(entry.path());
            } else {
                lift = false;
                break;
            }
        }
        if (!lift || only.empty()) return;
        // The folder's contents up; a note whose name the folder also has stays the folder's.
        const fs::path moved = staging / from_utf8(".lifting");
        fs::rename(only, moved, error);
        if (error) return;
        for (const auto& entry : fs::directory_iterator(moved, error)) {
            const fs::path target = staging / entry.path().filename();
            if (fs::exists(target, error)) fs::remove_all(target, error);
            fs::rename(entry.path(), target, error);
        }
        fs::remove_all(moved, error);
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
    for (;;) {
        const int next = archive_read_next_header(reader, &entry);
        if (next == ARCHIVE_EOF) break;
        if (next == ARCHIVE_FATAL || next == ARCHIVE_FAILED) {
            const char* why = archive_error_string(reader);
            result.message = std::string("THE ARCHIVE IS DAMAGED OR UNSUPPORTED: ") + (why ? why : "");
            return false;
        }
        if (writer.cancelled()) {
            result.message = "CANCELLED";
            return false;
        }
        if (archive_entry_is_encrypted(entry)) {
            result.message = "THE ARCHIVE IS PASSWORD-PROTECTED: EXTRACT IT YOURSELF AND IMPORT THE FOLDER";
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
            result.message = "CANNOT WRITE " + utf8(relative.filename()) + " (IS THE DISK FULL?)";
            return false;
        }
        writer.state.current = utf8(relative);
        for (;;) {
            const la_ssize_t read = archive_read_data(reader, buffer.data(), buffer.size());
            if (read == 0) break;
            if (read < 0) {
                const char* why = archive_error_string(reader);
                result.message = "CANNOT READ " + utf8(relative.filename()) + ": " + (why ? why : "");
                return false;
            }
            out.write(buffer.data(), read);
            if (!out) {
                result.message = "CANNOT WRITE " + utf8(relative.filename()) + " (IS THE DISK FULL?)";
                return false;
            }
            result.bytes += static_cast<std::uint64_t>(read);
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
            result.message = "CANNOT COPY " + utf8(relative.filename()) + ": " + copy_error.message();
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
                        const std::function<void(const ImportProgress&)>& progress, const std::atomic<bool>* cancel) {
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
        if (copied) result.message = "THE " + std::string(folder ? "FOLDER" : "ARCHIVE") + " HOLDS NO FILES";
        fs::remove_all(writer.staging, error);
        result.ok = false;
        return result;
    }
    if (!folder) lift_top_folder(writer.staging);
    // In place: an existing mod of that name is replaced (its settings, kept by name, stay).
    const fs::path final_folder = mods_folder / from_utf8(result.id);
    fs::path previous;
    if (fs::exists(final_folder, error)) {
        previous = staging_folder(mods_folder, result.id + "-previous");
        fs::rename(final_folder, previous, error);
        if (error) {
            fs::remove_all(writer.staging, error);
            result.message = "THE MOD " + result.id + " IS IN USE: CLOSE WHAT HAS ITS FILES OPEN AND TRY AGAIN";
            return result;
        }
        result.replaced = true;
    }
    fs::rename(writer.staging, final_folder, error);
    if (error) {
        if (!previous.empty()) fs::rename(previous, final_folder, error);
        fs::remove_all(writer.staging, error);
        result.message = "CANNOT MOVE THE MOD INTO PLACE";
        result.replaced = false;
        return result;
    }
    if (!previous.empty()) fs::remove_all(previous, error);
    writer.state.done = writer.state.total;
    writer.report(true);
    // What it holds, as the MODS page will list it.
    for (const ModInfo& mod : scan_mods()) {
        if (mod.id != result.id) continue;
        for (const auto& [on, name] : {std::pair{mod.riivolution, "RIIVOLUTION"}, std::pair{mod.files || mod.loose, "FILES"},
                                       std::pair{mod.textures, "TEXTURES"}, std::pair{mod.movies, "MOVIES"}}) {
            if (on) result.kinds += result.kinds.empty() ? name : std::string(", ") + name;
        }
        if (result.kinds.empty() && !mod.problem.empty()) {
            result.message = "IMPORTED " + result.id + ", BUT " + mod.problem;
        }
    }
    result.ok = true;
    if (result.message.empty()) {
        result.message = (result.replaced ? "UPDATED " : "IMPORTED ") + result.id +
                         (result.kinds.empty() ? std::string() : " (" + result.kinds + ")");
    }
    return result;
}

bool remove_mod(const fs::path& mods_folder, const std::string& id, std::string* message) {
    const fs::path folder = mods_folder / from_utf8(id);
    std::error_code error;
    if (id.empty() || id.front() == '.' || !fs::is_directory(folder, error)) {
        if (message != nullptr) *message = "NO MOD " + id;
        return false;
    }
#if defined(_WIN32)
    // To the recycle bin, so it can be restored.
    std::wstring from = folder.wstring();
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted && !fs::exists(folder, error)) {
        if (message != nullptr) *message = "MOVED " + id + " TO THE RECYCLE BIN";
        return true;
    }
#endif
    fs::remove_all(folder, error);
    if (error) {
        if (message != nullptr) *message = "CANNOT REMOVE " + id + ": " + error.message();
        return false;
    }
    if (message != nullptr) *message = "REMOVED " + id;
    return true;
}

}  // namespace galaxy::mods
