// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's mods services (contract services-mods): `mods` (the session's list), `mods.ids` (numbers in the game's
// numbered spaces, kept in the record <settings>/mod-ids.json) and `mods.install` (the import screen's request; no fs
// yet, so LUMA_E_ABSENT after its checks).

#include "galaxy/luma_mods_service.h"

#include "galaxy/atomic_file.h"
#include "galaxy/luma.h"
#include "galaxy/luma_fs_service.h"
#include "galaxy/mod_log.h"
#include "galaxy/mod_plugins.h"
#include "galaxy/mods.h"

#include <luma_modlog.h>
#include <luma_mods.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace galaxy::luma_mods {
namespace {

constexpr std::uint32_t kLow = 0x01000000u;  // every 1.0 space's mods' range (section 3)
constexpr std::uint32_t kHigh = 0x01FFFFFFu;
constexpr std::uint64_t kSpan = std::uint64_t{kHigh} - kLow + 1u;
constexpr std::size_t kNameMost = 64u;        // a name: 1 to 64 of a-z 0-9 _ - . (item 5)
constexpr std::size_t kModIdMost = 64u;       // a .luma's id (luma.cpp valid_id)
constexpr std::uint64_t kRecordMost = 16u << 20u;  // a record bigger than this is not ours
constexpr std::uint32_t kInfoLeast = static_cast<std::uint32_t>(sizeof(LumaModsInfo));  // 1.0's whole struct
static_assert(sizeof(LumaModsInfo) == 260u, "LumaModsInfo 1.0 is 260 bytes (contract section 2)");
static_assert(sizeof(LumaModsKind) == 4u && sizeof(LumaModsState) == 4u, "the enums of LumaModsInfo are 4 bytes");

const char* const kSpaces[] = {LUMA_MODS_SPACE_MESSAGE, LUMA_MODS_SPACE_SENSOR};

std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
void (*g_log)(const char*) = nullptr;  // set by providers() before the first mod

// ---- Calling rules (services-registry 4) ---------------------------------------------------------------------------

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr, const char* b = nullptr,
            const char* c = nullptr) noexcept {
    const char* args[3] = {a, b, c};
    std::uint32_t count = 0;
    while (count < 3u && args[count] != nullptr) {
        count++;
    }
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, count);
    }
}

// The mod known and live, on the game thread. LUMA_OK or the status to return.
std::int32_t enter(const LumaMod* mod, const char* where) noexcept {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr) {
        return LUMA_E_CANCELLED;
    }
    if (mod == nullptr || kernel->mod_id(mod) == nullptr) {
        kernel->unknown_mod(where);
        return LUMA_E_ARGUMENT;
    }
    if (kernel->mod_live(mod) == 0) {
        return LUMA_E_CANCELLED;
    }
    if (kernel->game_thread() == 0) {
        report(mod, where, 100u);  // Service.Thread
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

std::int32_t argument(const LumaMod* mod, const char* where, const char* name) noexcept {
    report(mod, where, 101u, name);  // Service.Argument
    return LUMA_E_ARGUMENT;
}

std::int32_t struct_size(const LumaMod* mod, const char* where, std::uint32_t found) noexcept {
    char found_text[16];
    char least_text[16];
    std::snprintf(found_text, sizeof(found_text), "%u", found);
    std::snprintf(least_text, sizeof(least_text), "%u", kInfoLeast);
    report(mod, where, 103u, "LumaModsInfo", found_text, least_text);  // Service.StructSize
    return LUMA_E_ARGUMENT;
}

// `text` into `out` (`size` bytes with its NUL), cut at a character's end (registry 3, R-5c).
void copy_text(char* out, std::size_t size, std::string_view text) noexcept {
    std::size_t length = std::min(text.size(), size - 1u);
    if (length < text.size()) {
        while (length > 0u && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
            length--;
        }
    }
    std::memcpy(out, text.data(), length);
    std::memset(out + length, 0, size - length);
}

// ---- mods: the session's list (items 3, 4) -------------------------------------------------------------------------

std::vector<LumaModsInfo> g_list;  // game thread; made at the first call, after the start recorded the session
bool g_list_made = false;

LumaModsState state_of(const galaxy::mods::ModInfo& mod, const std::vector<galaxy::mods::NotStarted>& not_started) {
    if (!mod.enabled) {
        return LUMA_MODS_OFF;
    }
    if (!mod.problem.empty()) {
        return LUMA_MODS_REFUSED;
    }
    for (const galaxy::mods::NotStarted& record : not_started) {
        if (record.mod_id == mod.id) {
            // The loader's refusals (L5xx) are the check's; the order's (L800) and the host's sentences (consent
            // waiting) leave a mod on that did not start.
            return record.code >= 500u && record.code < 600u ? LUMA_MODS_REFUSED : LUMA_MODS_NOT_STARTED;
        }
    }
    // Code for this platform the player has not allowed (consent waiting or Not now): the plan never hands it to the
    // loader (mods.cpp, "its code isn't run"), so no record says so.
    if (!mod.code_token.empty() && !mod.code_allowed) {
        return LUMA_MODS_NOT_STARTED;
    }
    return LUMA_MODS_RUNNING;
}

void make_list() {
    if (g_list_made) {
        return;
    }
    g_list_made = true;
    const galaxy::mods::SessionMods session = galaxy::mods::session_mods();
    const std::vector<galaxy::mods::NotStarted> not_started = galaxy::mods::plugins_not_started();
    g_list.reserve(session.started_with.size());
    std::uint32_t order = 0;
    for (const galaxy::mods::ModInfo& mod : session.started_with) {
        LumaModsInfo info{};
        info.size = kInfoLeast;
        info.order = ++order;
        info.kind = mod.package       ? LUMA_MODS_LUMA
                    : mod.riivolution ? LUMA_MODS_RIIVOLUTION
                    : mod.textures    ? LUMA_MODS_TEXTURES
                                      : LUMA_MODS_FILES;
        info.state = state_of(mod, not_started);
        info.has_code = mod.code_token.empty() ? 0u : 1u;
        copy_text(info.id, sizeof(info.id), mod.id);
        // pack_title is the name the Mods page shows: a .luma's name in the played language, a pack's title.
        copy_text(info.name, sizeof(info.name), mod.pack_title.empty() ? std::string_view(mod.id) : std::string_view(mod.pack_title));
        copy_text(info.version, sizeof(info.version), mod.luma ? std::string_view(mod.luma->version) : std::string_view());
        g_list.push_back(info);
    }
}

// The 1.0 fields into the mod's struct: whole fields only, none past its size (registry 3's size rule).
void give(const LumaModsInfo& info, LumaModsInfo* out) {
    const std::uint32_t size = out->size;
    std::memcpy(out, &info, std::min<std::size_t>(size, sizeof(info)));
    out->size = size;
}

std::uint32_t mods_count(const LumaMod* mod) {
    if (enter(mod, "mods.count") != LUMA_OK) {
        return 0;
    }
    make_list();
    return static_cast<std::uint32_t>(g_list.size());
}

std::int32_t mods_at(const LumaMod* mod, std::uint32_t index, LumaModsInfo* out) {
    const char* const where = "mods.at";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (out->size < kInfoLeast) {
        return struct_size(mod, where, out->size);
    }
    make_list();
    if (index >= g_list.size()) {
        return LUMA_E_NOT_FOUND;
    }
    give(g_list[index], out);
    return LUMA_OK;
}

std::int32_t mods_find(const LumaMod* mod, const char* id, LumaModsInfo* out) {
    const char* const where = "mods.find";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (id == nullptr) {
        return argument(mod, where, "id");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (out->size < kInfoLeast) {
        return struct_size(mod, where, out->size);
    }
    make_list();
    for (const LumaModsInfo& info : g_list) {
        if (std::strcmp(info.id, id) == 0) {
            give(info, out);
            return LUMA_OK;
        }
    }
    return LUMA_E_NOT_FOUND;
}

// ---- mods.ids: the record (items 5 to 8) ---------------------------------------------------------------------------

bool known_space(std::string_view space) noexcept {
    return std::any_of(std::begin(kSpaces), std::end(kSpaces), [space](const char* known) { return space == known; });
}

bool valid_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > kNameMost) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

bool valid_mod_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > kModIdMost) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

// A key `<mod id>/<name>` as the record holds it.
bool valid_key(std::string_view key) noexcept {
    const std::size_t slash = key.find('/');
    return slash != std::string_view::npos && valid_mod_id(key.substr(0, slash)) && valid_name(key.substr(slash + 1u));
}

// fnv1a32 of the space's bytes, a 0 byte, then the key's (section 4): the first number tried, the same everywhere.
std::uint32_t derived(std::string_view space, std::string_view key) noexcept {
    std::uint32_t hash = 2166136261u;
    const auto mix = [&hash](unsigned char byte) {
        hash ^= byte;
        hash *= 16777619u;
    };
    for (const char c : space) {
        mix(static_cast<unsigned char>(c));
    }
    mix(0u);
    for (const char c : key) {
        mix(static_cast<unsigned char>(c));
    }
    return static_cast<std::uint32_t>(kLow + std::uint64_t{hash} % kSpan);
}

struct Space {
    std::map<std::string, std::uint32_t> keys;             // the record's order on disk: by key
    std::unordered_map<std::uint32_t, std::string> owners;  // number -> key
};

struct Record {
    std::mutex lock;
    bool read = false;
    std::map<std::string, Space> spaces;  // spaces of a later Nebula are kept as read and written back
    std::uint64_t changes = 0;            // bumped by each change; the writer writes until it has caught up
    std::uint64_t written = 0;
    bool writer = false;
    std::condition_variable wake;
    std::mutex io;  // one write at a time
};

Record& record() {
    static Record r;
    return r;
}

std::filesystem::path record_path() {
    const std::filesystem::path settings = galaxy::mods::mods_settings_file();
    return settings.empty() ? std::filesystem::path() : settings.parent_path() / "mod-ids.json";
}

// ---- The record's form: {"format": 1, "spaces": {"<space>": {"<key>": number}}} --------------------------------------
// A strict reader for exactly that form; anything else makes the record unreadable (item 7).

struct Reader {
    std::string_view text;
    std::size_t at = 0;

    void blank() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            at++;
        }
    }
    bool take(char c) {
        blank();
        if (at < text.size() && text[at] == c) {
            at++;
            return true;
        }
        return false;
    }
    // A string without control characters; escapes \" \\ \/ only (the record's keys need none).
    std::optional<std::string> string() {
        if (!take('"')) {
            return std::nullopt;
        }
        std::string out;
        while (at < text.size()) {
            const char c = text[at++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20u) {
                return std::nullopt;
            }
            if (c == '\\') {
                if (at >= text.size() || (text[at] != '"' && text[at] != '\\' && text[at] != '/')) {
                    return std::nullopt;
                }
                out.push_back(text[at++]);
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }
    // A whole number from 0 to 2^32 - 1, no sign, no fraction.
    std::optional<std::uint32_t> number() {
        blank();
        const std::size_t first = at;
        std::uint64_t value = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            value = value * 10u + static_cast<std::uint64_t>(text[at] - '0');
            if (value > 0xFFFFFFFFull) {
                return std::nullopt;
            }
            at++;
        }
        if (at == first || (at - first > 1u && text[first] == '0')) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(value);
    }
    // `{ "k": <each>, ... }`, each member read by `member` (false: unreadable).
    template <typename Member>
    bool object(Member member) {
        if (!take('{')) {
            return false;
        }
        if (take('}')) {
            return true;
        }
        do {
            std::optional<std::string> key = string();
            if (!key || !take(':') || !member(*key)) {
                return false;
            }
        } while (take(','));
        return take('}');
    }
};

// Parses `text` into `spaces`; false when it is not the record's form or breaks a rule (a number out of range, one
// number held twice in a space, a key twice).
bool parse_record(std::string_view text, std::map<std::string, Space>& spaces) {
    Reader in{text};
    bool format_seen = false;
    bool spaces_seen = false;
    const bool shaped = in.object([&](const std::string& field) {
        if (field == "format" && !format_seen) {
            format_seen = true;
            const std::optional<std::uint32_t> format = in.number();
            return format.has_value() && *format == 1u;
        }
        if (field == "spaces" && !spaces_seen) {
            spaces_seen = true;
            return in.object([&](const std::string& name) {
                if (spaces.count(name) != 0u || name.empty() || name.size() > 31u) {
                    return false;
                }
                Space& space = spaces[name];
                return in.object([&](const std::string& key) {
                    const std::optional<std::uint32_t> number = in.number();
                    if (!number || *number < kLow || *number > kHigh || !valid_key(key) || space.keys.count(key) != 0u ||
                        space.owners.count(*number) != 0u) {
                        return false;
                    }
                    space.keys.emplace(key, *number);
                    space.owners.emplace(*number, key);
                    return true;
                });
            });
        }
        return false;
    });
    in.blank();
    return shaped && format_seen && spaces_seen && in.at == text.size();
}

std::string write_record(const std::map<std::string, Space>& spaces) {
    std::ostringstream out;
    out << "{\n  \"format\": 1,\n  \"spaces\": {";
    bool first_space = true;
    for (const auto& [name, space] : spaces) {
        out << (first_space ? "\n" : ",\n") << "    \"" << name << "\": {";
        first_space = false;
        bool first_key = true;
        for (const auto& [key, number] : space.keys) {
            out << (first_key ? "\n" : ",\n") << "      \"" << key << "\": " << number;
            first_key = false;
        }
        out << (first_key ? "}" : "\n    }");
    }
    out << (first_space ? "}\n}\n" : "\n  }\n}\n");
    return out.str();
}

void log_line(const char* line) {
    if (g_log != nullptr) {
        g_log(line);
    }
}

// The record's writer (item 7): whole file, renamed into place, off the game thread; it writes until it has caught up.
void writer_main() {
    Record& r = record();
    for (;;) {
        std::string contents;
        std::uint64_t changes = 0;
        {
            std::unique_lock<std::mutex> lock(r.lock);
            r.wake.wait(lock, [&r] { return r.written != r.changes; });
            changes = r.changes;
            contents = write_record(r.spaces);
        }
        const std::filesystem::path path = record_path();
        bool done = true;
        if (!path.empty()) {
            const std::lock_guard<std::mutex> io(r.io);
            done = galaxy::io::write_file_atomically(path, contents);
        }
        if (!done) {
            log_line("LumaCore mods.ids: the record of mods' numbers could not be written; trying again in 2 s");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        const std::lock_guard<std::mutex> lock(r.lock);
        r.written = std::max(r.written, changes);
    }
}

// r.lock held. Marks a change and wakes (or starts) the writer.
void changed(Record& r) {
    r.changes++;
    if (!r.writer) {
        r.writer = true;
        std::thread(writer_main).detach();
    }
    r.wake.notify_one();
}

// r.lock held. Reads the record once a session (item 7). Returns true when it was unreadable (the caller logs the
// Error once the lock is let go).
bool ensure_read(Record& r) {
    if (r.read) {
        return false;
    }
    r.read = true;
    const std::filesystem::path path = record_path();
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return false;
    }
    std::string text;
    bool readable = false;
    {
        std::ifstream in(path, std::ios::binary);
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (in && !error && size <= kRecordMost) {
            text.resize(static_cast<std::size_t>(size));
            readable = static_cast<bool>(in.read(text.data(), static_cast<std::streamsize>(text.size())));
        }
    }
    std::map<std::string, Space> spaces;
    if (readable && parse_record(text, spaces)) {
        r.spaces = std::move(spaces);
        return false;
    }
    // Kept aside as ids.json.bad; the session starts a new record, numbers still derived.
    std::filesystem::path bad = path;
    bad += ".bad";
    std::filesystem::remove(bad, error);
    std::filesystem::rename(path, bad, error);
    if (error) {
        log_line("LumaCore mods.ids: the unreadable record could not be moved aside; it will be replaced");
    }
    return true;
}

void unreadable_error() {
    galaxy::modlog::add_row(LUMA_MODLOG_CODE, LUMA_MODLOG_ERROR, "", 161u, "mod-ids.json", "Mods.IdsUnreadable", {});
}

std::int32_t ids_get(const LumaMod* mod, const char* space, const char* name, std::uint32_t* out) {
    const char* const where = "mods.ids.get";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (space == nullptr || !known_space(space)) {
        return argument(mod, where, "space");
    }
    if (name == nullptr || !valid_name(name)) {
        return argument(mod, where, "name");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const std::string mod_id = kernel->mod_id(mod);
    const std::string key = mod_id + "/" + name;
    Record& r = record();
    bool unreadable = false;
    bool full = false;
    std::uint32_t number = 0;
    std::string moved_from;  // the key holding the derived number, when this one moved
    {
        const std::lock_guard<std::mutex> lock(r.lock);
        unreadable = ensure_read(r);
        Space& held = r.spaces[space];
        if (const auto found = held.keys.find(key); found != held.keys.end()) {
            number = found->second;
        } else if (held.keys.size() >= kSpan) {
            full = true;
        } else {
            const std::uint32_t first = derived(space, key);
            number = first;
            while (held.owners.count(number) != 0u) {
                number = number == kHigh ? kLow : number + 1u;
            }
            if (number != first) {
                moved_from = held.owners[first];
            }
            held.keys.emplace(key, number);
            held.owners.emplace(number, key);
            changed(r);
        }
    }
    if (unreadable) {
        unreadable_error();
    }
    if (full) {
        report(mod, where, 104u, "16777216");  // Service.Limit
        return LUMA_E_LIMIT;
    }
    if (!moved_from.empty()) {
        char number_text[16];
        std::snprintf(number_text, sizeof(number_text), "%u", number);
        galaxy::modlog::add_row(LUMA_MODLOG_CODE, LUMA_MODLOG_NOTE, mod_id, 160u, "", "Mods.IdMoved",
                                {{"key", key}, {"number", number_text}, {"space", space}, {"other", moved_from}});
    }
    *out = number;
    return LUMA_OK;
}

std::int32_t ids_of(const LumaMod* mod, const char* space, const char* mod_id, const char* name, std::uint32_t* out) {
    const char* const where = "mods.ids.of";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (space == nullptr || !known_space(space)) {
        return argument(mod, where, "space");
    }
    if (mod_id == nullptr || !valid_mod_id(mod_id)) {
        return argument(mod, where, "mod_id");
    }
    if (name == nullptr || !valid_name(name)) {
        return argument(mod, where, "name");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    const std::string key = std::string(mod_id) + "/" + name;
    Record& r = record();
    bool unreadable = false;
    std::optional<std::uint32_t> number;
    {
        const std::lock_guard<std::mutex> lock(r.lock);
        unreadable = ensure_read(r);
        if (const auto held = r.spaces.find(space); held != r.spaces.end()) {
            if (const auto found = held->second.keys.find(key); found != held->second.keys.end()) {
                number = found->second;
            }
        }
    }
    if (unreadable) {
        unreadable_error();
    }
    if (!number) {
        return LUMA_E_NOT_FOUND;
    }
    *out = *number;
    return LUMA_OK;
}

// ---- mods.install (item 9) -----------------------------------------------------------------------------------------

// A path of fs's rule (services-fs 4: inside the mod's data folder, the same on every platform) ending ".luma".
bool valid_install_path(std::string_view path) noexcept {
    return path.size() > 5u && path.substr(path.size() - 5u) == ".luma" && galaxy::luma_fs::path_in_rule(path, false);
}

std::int32_t install_request(const LumaMod* mod, const char* path, LumaEventFn fn, void*, LumaHandle* out) {
    const char* const where = "mods.install.request";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (path == nullptr || !valid_install_path(path)) {
        return argument(mod, where, "path");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    // A mod's data folder is fs 2.3's; until it exists no path names a file (item 9).
    return LUMA_E_ABSENT;
}

std::int32_t install_cancel(const LumaMod* mod, LumaHandle) {
    const char* const where = "mods.install.cancel";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    // No request is ever open before fs 2.3.
    report(mod, where, 102u);  // Service.Handle
    return LUMA_E_HANDLE;
}

// ---- The providers -------------------------------------------------------------------------------------------------

// Set once at the host's start, before any mod or page asks (set_save_half, set_chunk_half).
SaveHalf g_save;
LumaSaveKept g_chunk_kept = nullptr;
LumaSaveForget g_chunk_forget = nullptr;

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaModsService kMods = {
    {sizeof(LumaModsService), LUMA_MODS_MAJOR, LUMA_MODS_MINOR},
    mods_count,
    mods_at,
    mods_find,
};

const LumaModsIdsService kIds = {
    {sizeof(LumaModsIdsService), LUMA_MODS_IDS_MAJOR, LUMA_MODS_IDS_MINOR},
    ids_get,
    ids_of,
};

const LumaModsInstallService kInstall = {
    {sizeof(LumaModsInstallService), LUMA_MODS_INSTALL_MAJOR, LUMA_MODS_INSTALL_MINOR},
    install_request,
    install_cancel,
};

// No per-mod state: the list is the session's, the record everyone's, and no request is open before fs.
const LumaServiceProvider kModsProvider = {
    sizeof(LumaServiceProvider), LUMA_MODS_NAME, &kMods.base, nullptr, nullptr, 0u, nullptr,
    nullptr, nullptr, nullptr, registered,
};
const LumaServiceProvider kIdsProvider = {
    sizeof(LumaServiceProvider), LUMA_MODS_IDS_NAME, &kIds.base, nullptr, nullptr, 0u, nullptr,
    nullptr, nullptr, nullptr, registered,
};
const LumaServiceProvider kInstallProvider = {
    sizeof(LumaServiceProvider), LUMA_MODS_INSTALL_NAME, &kInstall.base, nullptr, nullptr, 0u, nullptr,
    nullptr, nullptr, nullptr, registered,
};
const LumaServiceProvider* const kProviders[] = {&kModsProvider, &kIdsProvider, &kInstallProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept {
    g_log = log;
    return kProviders;
}

void kept(std::vector<KeptMod>& out) {
    out.clear();
    std::map<std::string, std::uint32_t> numbers;  // by mod id
    Record& r = record();
    bool unreadable = false;
    {
        const std::lock_guard<std::mutex> lock(r.lock);
        unreadable = ensure_read(r);
        for (const auto& [name, space] : r.spaces) {
            for (const auto& [key, number] : space.keys) {
                numbers[key.substr(0, key.find('/'))]++;
            }
        }
    }
    if (unreadable) {
        unreadable_error();
    }
    // Everything a removed mod keeps (services-mods 8): ids with numbers, and ids the save halves keep values for.
    std::map<std::string, std::uint64_t> chunk_bytes;
    if (g_chunk_kept != nullptr) {
        g_chunk_kept([](void* user, const char* id, std::uint64_t bytes) {
            (*static_cast<std::map<std::string, std::uint64_t>*>(user))[id] += bytes;
        }, &chunk_bytes);
        for (const auto& [id, bytes] : chunk_bytes) {
            if (valid_mod_id(id)) {
                numbers.emplace(id, 0u);
            }
        }
    }
    if (g_save.ids != nullptr) {
        std::vector<std::string> saved;
        g_save.ids(saved);
        for (std::string& id : saved) {
            if (valid_mod_id(id)) {
                numbers.emplace(std::move(id), 0u);
            }
        }
    }
    // And ids with a data folder of fs (services-fs 3).
    {
        std::vector<std::string> data;
        galaxy::luma_fs::data_ids(data);
        for (std::string& id : data) {
            if (valid_mod_id(id)) {
                numbers.emplace(std::move(id), 0u);
            }
        }
    }
    const galaxy::mods::SessionMods session = galaxy::mods::session_mods();
    const std::filesystem::path folder = galaxy::mods::mods_folder();
    for (const auto& [id, count] : numbers) {
        const bool listed = std::any_of(session.started_with.begin(), session.started_with.end(),
                                        [&id](const galaxy::mods::ModInfo& mod) { return mod.id == id; });
        std::error_code error;
        if (listed || (!folder.empty() && std::filesystem::exists(folder / id, error))) {
            continue;
        }
        const auto in_chunks = chunk_bytes.find(id);
        const std::uint64_t bytes = (g_save.bytes != nullptr ? g_save.bytes(id) : 0u) +
                                    (in_chunks != chunk_bytes.end() ? in_chunks->second : 0u);
        const std::uint64_t data_bytes = galaxy::luma_fs::data_bytes(id);
        // A data folder with no file in it still holds the id's place: FORGET removes it too.
        const bool data_folder = data_bytes != 0u || galaxy::luma_fs::has_data_folder(id);
        if (count != 0u || bytes != 0u || data_folder) {
            out.push_back({id, count, bytes, data_bytes});
        }
    }
}

void forget(std::string_view id) {
    Record& r = record();
    bool unreadable = false;
    {
        const std::lock_guard<std::mutex> lock(r.lock);
        unreadable = ensure_read(r);
        bool any = false;
        for (auto& [name, space] : r.spaces) {
            for (auto it = space.keys.begin(); it != space.keys.end();) {
                if (std::string_view(it->first).substr(0, it->first.find('/')) == id) {
                    space.owners.erase(it->second);
                    it = space.keys.erase(it);
                    any = true;
                } else {
                    ++it;
                }
            }
        }
        if (any) {
            changed(r);
        }
    }
    if (unreadable) {
        unreadable_error();
    }
    if (valid_mod_id(id)) {
        if (g_save.forget != nullptr) {
            g_save.forget(id);
        }
        if (g_chunk_forget != nullptr) {
            g_chunk_forget(std::string(id).c_str());
        }
        galaxy::luma_fs::forget_data(id);  // its data folder, after every fs request queued before
    }
    galaxy::modlog::add_row(LUMA_MODLOG_CODE, LUMA_MODLOG_NOTE, id, 162u, "", "Mods.Forgotten", {});
}

std::uint32_t take_over_ids(std::string_view old_id, std::string_view new_id) {
    if (!valid_mod_id(old_id) || !valid_mod_id(new_id) || old_id == new_id) {
        return 0;
    }
    Record& r = record();
    bool unreadable = false;
    std::uint32_t moved = 0;
    {
        const std::lock_guard<std::mutex> lock(r.lock);
        unreadable = ensure_read(r);
        for (auto& [name, space] : r.spaces) {
            std::vector<std::pair<std::string, std::uint32_t>> taken;
            for (auto it = space.keys.begin(); it != space.keys.end();) {
                if (std::string_view(it->first).substr(0, it->first.find('/')) == old_id) {
                    taken.emplace_back(it->first.substr(it->first.find('/') + 1u), it->second);
                    space.owners.erase(it->second);
                    it = space.keys.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& [rest, number] : taken) {
                const std::string key = std::string(new_id) + "/" + rest;
                if (space.keys.count(key) != 0u) {
                    continue;  // the new id asked for it already: its number stays, the old one is free again
                }
                space.keys.emplace(key, number);
                space.owners.emplace(number, key);
                moved++;
            }
            if (!taken.empty()) {
                changed(r);
            }
        }
    }
    if (unreadable) {
        unreadable_error();
    }
    return moved;
}

void set_save_half(const SaveHalf& half) noexcept {
    g_save = half;
}

void set_chunk_half(LumaSaveKept kept_ids, LumaSaveForget forget) noexcept {
    g_chunk_kept = kept_ids;
    g_chunk_forget = forget;
}

}  // namespace galaxy::luma_mods
