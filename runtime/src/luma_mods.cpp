// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's mods services (docs/modding/services/mods.md): `mods` (the session's list), `mods.ids` (numbers in the
// game's numbered spaces, kept in the record <settings>/mod-ids.json) and `mods.install` (the import screen's request;
// no fs yet, so LUMA_E_ABSENT after its checks).

#include "galaxy/luma_mods_service.h"

#include "galaxy/atomic_file.h"
#include "galaxy/luma.h"
#include "galaxy/luma_fs_service.h"
#include "galaxy/mod_log.h"
#include "galaxy/mod_screens.h"
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
#include <memory>
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
static_assert(sizeof(LumaModsInfo) == 260u, "LumaModsInfo 1.0 is 260 bytes (mods.md section 2)");
static_assert(sizeof(LumaModsKind) == 4u && sizeof(LumaModsState) == 4u, "the enums of LumaModsInfo are 4 bytes");

const char* const kSpaces[] = {LUMA_MODS_SPACE_MESSAGE, LUMA_MODS_SPACE_SENSOR};

std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
void (*g_log)(const char*) = nullptr;  // set by providers() before the first mod

// ---- Calling rules (services/registry.md 4)
// ---------------------------------------------------------------------------

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

// `text` into `out` (`size` bytes with its NUL), cut at a character's end (services/registry.md 3).
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
// Game thread: the ids of the mods taken out while the game runs (their services ended), until a reload begins them
// again. A running mod in the list reads TAKEN_OUT while its id is here.
std::vector<std::string> g_taken_out;

bool taken_out(std::string_view id) {
    return std::find(g_taken_out.begin(), g_taken_out.end(), id) != g_taken_out.end();
}

// The list's line of `id` follows a take-out or a reload: RUNNING and TAKEN_OUT only, the other states are the start's.
void follow_take_out(std::string_view id, bool out) {
    for (LumaModsInfo& info : g_list) {
        if (id != info.id) continue;
        if (out && info.state == LUMA_MODS_RUNNING) info.state = LUMA_MODS_TAKEN_OUT;
        if (!out && info.state == LUMA_MODS_TAKEN_OUT) info.state = LUMA_MODS_RUNNING;
    }
}

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
        if (info.state == LUMA_MODS_RUNNING && taken_out(mod.id)) info.state = LUMA_MODS_TAKEN_OUT;
        info.has_code = mod.code_token.empty() ? 0u : 1u;
        copy_text(info.id, sizeof(info.id), mod.id);
        // pack_title is the name the Mods page shows: a .luma's name in the played language, a pack's title.
        copy_text(info.name, sizeof(info.name), mod.pack_title.empty() ? std::string_view(mod.id) : std::string_view(mod.pack_title));
        copy_text(info.version, sizeof(info.version), mod.luma ? std::string_view(mod.luma->version) : std::string_view());
        g_list.push_back(info);
    }
}

// The 1.0 fields into the mod's struct: whole fields only, none past its size (services/registry.md 3's size rule).
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
    std::condition_variable caught_up;  // the writer has written every change so far (flush_ids waits on it)
    std::mutex io;                      // one write at a time
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
        {
            const std::lock_guard<std::mutex> lock(r.lock);
            r.written = std::max(r.written, changes);
        }
        r.caught_up.notify_all();
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

// A path of fs's rule (services/fs.md 4: inside the mod's data folder, the same on every platform) ending ".luma".
bool valid_install_path(std::string_view path) noexcept {
    return path.size() > 5u && path.substr(path.size() - 5u) == ".luma" && galaxy::luma_fs::path_in_rule(path, false);
}

// The one open request (the import screen shows one at a time: a second is LUMA_E_BUSY). Game thread only: the request,
// the screen's answer (run_screen_answers) and the delivery all run there.
struct InstallRequest;
struct InstallNode {
    LumaEventNode event;     // first, and both plain members: the kernel's node is the InstallNode
    InstallRequest* owner;
};
struct InstallRequest {
    InstallNode node{};
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::string asker;
    std::int32_t status = LUMA_OK;  // the screen's answer, given at delivery
    bool posted = false;            // queued with the kernel: freed by deliver or drop, not before
    bool closed = false;            // cancelled, or the mod is gone: no event
};
InstallRequest* g_install = nullptr;  // game thread
LumaHandle g_install_next = 1;        // game thread: never 0, never given twice

void install_free(InstallRequest* request) {
    if (g_install == request) {
        g_install = nullptr;
    }
    delete request;
}

// The DONE event (closed after it: the request goes).
void install_deliver(LumaEventNode* node) {
    InstallRequest* const request = reinterpret_cast<InstallNode*>(node)->owner;
    request->posted = false;
    if (!request->closed) {
        LumaEvent event{};
        event.size = sizeof(event);
        event.kind = LUMA_MODS_INSTALL_EVENT_DONE;
        event.status = request->status;
        event.handle = request->handle;
        if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
            (void)kernel->call(request->node.event.mod, LUMA_MODS_INSTALL_NAME, request->fn, request->user, &event);
        }
    }
    install_free(request);
}

void install_drop(LumaEventNode* node) {
    InstallRequest* const request = reinterpret_cast<InstallNode*>(node)->owner;
    request->posted = false;
    install_free(request);
}

std::int32_t install_request(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out) {
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
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const char* const id = kernel != nullptr ? kernel->mod_id(mod) : nullptr;
    if (id == nullptr) {
        return LUMA_E_CANCELLED;
    }
    // The player's DON'T ASK AGAIN for this mod (J-SM6): an answer, no screen.
    if (!galaxy::mods::install_ask_allowed(id)) {
        return LUMA_E_DENIED;
    }
    if (g_install != nullptr || galaxy::mods::player_screen_up()) {
        return LUMA_E_BUSY;
    }
    // The file at the call (J-SM5): a short walk of the mod's own data folder, no link followed (services/fs.md 5).
    std::filesystem::path file;
    if (galaxy::luma_fs::find_data_file(id, path, &file) != LUMA_OK) {
        return LUMA_E_NOT_FOUND;
    }
    InstallRequest* request = nullptr;
    try {
        request = new InstallRequest();
        request->asker = id;
    } catch (...) {
        delete request;
        return LUMA_E_MEMORY;
    }
    request->node.owner = request;
    request->node.event.mod = mod;
    request->node.event.deliver = install_deliver;
    request->node.event.drop = install_drop;
    request->handle = g_install_next++;
    request->fn = fn;
    request->user = user;
    const LumaHandle handle = request->handle;
    g_install = request;
    // The import screen answers on the game thread at a frame's start, never inside this call. The answer finds the
    // request by its handle: one closed meanwhile (cancel, the mod gone) or already answered takes none.
    std::int32_t status = LUMA_E_MEMORY;
    try {
        status = galaxy::mods::ask_install(file, request->asker, [handle](std::int32_t answer) {
            InstallRequest* const open = g_install;
            if (open == nullptr || open->handle != handle || open->closed || open->posted) {
                return;
            }
            open->status = answer;
            const LumaServiceKernel* const k = g_kernel.load(std::memory_order_acquire);
            if (k != nullptr && k->post(&open->node.event) == LUMA_OK) {
                open->posted = true;
            } else {
                install_free(open);  // the mod is going: nothing is said
            }
        });
    } catch (...) {
        status = LUMA_E_MEMORY;
    }
    if (status != LUMA_PENDING) {
        install_free(request);
        return status;  // LUMA_E_BUSY or LUMA_E_ABSENT (no menu in this host): answers
    }
    *out = handle;
    return LUMA_PENDING;
}

// Closes the open request of `mod` (cancel, or the mod gone): its screen closes as Not now, no event after.
void install_close(const LumaMod* mod) {
    InstallRequest* const request = g_install;
    if (request == nullptr || request->node.event.mod != mod || request->closed) {
        return;
    }
    request->closed = true;
    galaxy::mods::close_asks(request->asker);
    if (!request->posted) {
        install_free(request);  // queued: freed by deliver or drop
    }
}

std::int32_t install_cancel(const LumaMod* mod, LumaHandle handle) {
    const char* const where = "mods.install.cancel";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    const InstallRequest* const request = g_install;
    if (request == nullptr || request->node.event.mod != mod || request->handle != handle || request->closed) {
        report(mod, where, 102u);  // Service.Handle
        return LUMA_E_HANDLE;
    }
    install_close(mod);
    return LUMA_OK;
}

void install_mod_gone(void*, const LumaMod* mod) {
    install_close(mod);
}

// ---- mods: a mod's own options (section 8) -------------------------------------------------------------------------

struct ModOptions;
struct WatchNode {
    LumaEventNode event;  // first, and both plain members: the kernel's node is the WatchNode
    ModOptions* owner;    // its entry in g_options (never erased, so it stays put)
};

// One mod of the session's list that has options: their texts now, in luma.json order, and its watcher.
struct ModOptions {
    std::vector<std::pair<std::string, std::string>> values;  // key, text
    const LumaMod* watcher = nullptr;  // the running mod watching (watch_options with a fn)
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::vector<std::uint32_t> changed;  // places changed since the last delivery, in order, each once
    std::unique_ptr<WatchNode> node;
};

std::mutex g_options_lock;  // g_options and every ModOptions; any thread (the Mods page applies on its own)
std::map<std::string, ModOptions, std::less<>> g_options;
bool g_options_made = false;

// Code points of valid UTF-8, or -1 when it is not UTF-8.
std::int64_t code_points(std::string_view text) noexcept {
    std::int64_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80u ? 1u : (lead & 0xE0u) == 0xC0u ? 2u : (lead & 0xF0u) == 0xE0u ? 3u
                                   : (lead & 0xF8u) == 0xF0u ? 4u : 0u;
        if (length == 0u || i + length > text.size() || (length == 2u && lead < 0xC2u)) {
            return -1;
        }
        std::uint32_t point = length == 1u ? lead : lead & (0x7Fu >> length);
        for (std::size_t k = 1; k < length; k++) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u) {
                return -1;
            }
            point = (point << 6u) | (next & 0x3Fu);
        }
        if ((length == 3u && point < 0x800u) || (length == 4u && (point < 0x10000u || point > 0x10FFFFu)) ||
            (point >= 0xD800u && point <= 0xDFFFu)) {
            return -1;  // overlong, past Unicode, or a surrogate
        }
        i += length;
        count++;
    }
    return count;
}

// An option as the mod reads it (header): `chosen` is the Mods page's line for it (null: none, the default).
std::string option_text(const galaxy::luma::Option& entry, const galaxy::mods::ModOption* chosen, std::string_view mod_id) {
    if (entry.kind == "text") {
        if (chosen == nullptr || chosen->text.empty()) {
            return entry.fallback;
        }
        const std::int64_t points = code_points(chosen->text);
        if (points < 0 || points > static_cast<std::int64_t>(entry.max_length) ||
            chosen->text.find('\0') != std::string::npos) {
            if (g_log != nullptr) {
                const std::string line = "[mods] " + std::string(mod_id) + ": the text kept for its option " + entry.id +
                                         " is not UTF-8 of at most " + std::to_string(entry.max_length) +
                                         " characters; it reads the default";
                g_log(line.c_str());
            }
            return entry.fallback;
        }
        return chosen->text;
    }
    if (entry.kind == "key") {
        return entry.fallback;  // input's store: its binding_name once it exists
    }
    if (entry.kind == "number") {
        // The Mods page keeps only values of the range (scan_mods put a kept value outside it back to the default).
        const std::optional<std::string> value = chosen != nullptr ? galaxy::luma::number_value(entry, chosen->text) : std::nullopt;
        return value ? *value : entry.fallback;
    }
    std::string id = entry.fallback;
    if (chosen != nullptr) {
        id = chosen->selected != 0u && chosen->selected <= chosen->choice_ids.size() ? chosen->choice_ids[chosen->selected - 1u]
                                                                                     : std::string("off");
    }
    if (entry.kind == "toggle") {
        return id == "on" ? "1" : "0";
    }
    return id;  // a choice's value id
}

// The texts of `mod`'s options in luma.json order (empty for a mod without a manifest).
std::vector<std::pair<std::string, std::string>> texts_of(const galaxy::mods::ModInfo& mod) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!mod.luma) {
        return out;
    }
    for (const galaxy::luma::Option& entry : mod.luma->options) {
        const galaxy::mods::ModOption* chosen = nullptr;
        for (const galaxy::mods::ModOption& line : mod.options) {
            if (line.key == entry.id) {
                chosen = &line;
                break;
            }
        }
        out.emplace_back(entry.id, option_text(entry, chosen, mod.id));
    }
    return out;
}

// With g_options_lock: the session's values, once (the list the session started with, item 3).
void make_options_locked() {
    if (g_options_made) {
        return;
    }
    g_options_made = true;
    for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
        if (mod.luma && !mod.luma->options.empty()) {
            g_options[mod.id].values = texts_of(mod);
        }
    }
}

void deliver_options(LumaEventNode* node) {
    ModOptions* const owner = reinterpret_cast<WatchNode*>(node)->owner;
    const LumaMod* mod = nullptr;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::vector<std::uint32_t> changed;
    {
        const std::lock_guard<std::mutex> lock(g_options_lock);
        if (owner->fn == nullptr) {
            return;
        }
        mod = owner->watcher;
        fn = owner->fn;
        user = owner->user;
        changed.swap(owner->changed);
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    for (const std::uint32_t place : changed) {
        LumaEvent event{};
        event.size = sizeof(event);
        event.kind = LUMA_MODS_EVENT_OPTION;
        event.status = LUMA_OK;
        event.value = place;
        if (kernel == nullptr || kernel->call(mod, LUMA_MODS_NAME, fn, user, &event) != LUMA_OK) {
            return;  // the mod is gone: the rest are not its business
        }
    }
}

void drop_options(LumaEventNode*) {}  // the node is the ModOptions'; nothing was taken for it

std::int64_t mods_option(const LumaMod* mod, const char* key, char* buf, std::uint64_t size) {
    static constexpr const char* where = "mods.option";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (key == nullptr) {
        report(mod, where, 165u, "(null)");  // Mods.OptionUnknown
        return LUMA_E_NOT_FOUND;
    }
    if (std::strchr(key, '/') != nullptr) {
        report(mod, where, 166u, key);  // Mods.OptionNotOwn
        return LUMA_E_DENIED;
    }
    if (buf == nullptr && size != 0u) {
        return argument(mod, where, "buf");
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const char* const id = kernel->mod_id(mod);
    std::string text;
    bool found = false;
    {
        const std::lock_guard<std::mutex> lock(g_options_lock);
        make_options_locked();
        if (const auto it = g_options.find(std::string_view(id)); it != g_options.end()) {
            for (const auto& [option, value] : it->second.values) {
                if (option == key) {
                    text = value;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        report(mod, where, 165u, key);  // Mods.OptionUnknown
        return LUMA_E_NOT_FOUND;
    }
    const std::uint64_t length = text.size();
    if (buf == nullptr) {
        return static_cast<std::int64_t>(length);  // the length asked (size 0)
    }
    if (size < length + 1u) {
        char given[24];
        char needed[24];
        std::snprintf(given, sizeof(given), "%llu", static_cast<unsigned long long>(size));
        std::snprintf(needed, sizeof(needed), "%llu", static_cast<unsigned long long>(length + 1u));
        report(mod, where, 167u, given, key, needed);  // Mods.OptionBufferShort
        return LUMA_E_TOO_LARGE;
    }
    std::memcpy(buf, text.data(), static_cast<std::size_t>(length));
    buf[length] = '\0';
    return static_cast<std::int64_t>(length);
}

std::int32_t mods_watch_options(const LumaMod* mod, LumaEventFn fn, void* user) {
    if (const std::int32_t status = enter(mod, "mods.watch_options"); status != LUMA_OK) {
        return status;
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const std::string id = kernel->mod_id(mod);
    const std::lock_guard<std::mutex> lock(g_options_lock);
    make_options_locked();
    ModOptions& options = g_options[id];
    options.watcher = fn != nullptr ? mod : nullptr;
    options.fn = fn;
    options.user = fn != nullptr ? user : nullptr;
    if (fn == nullptr) {
        options.changed.clear();
    }
    return LUMA_OK;
}

// ---- mods: other mods stopping (watch_mods; LumaKit rule 4) ---------------------------------------------------------

struct StopWatch;
struct StopNode {
    LumaEventNode event;  // first, and both plain members: the kernel's node is the StopNode
    StopWatch* owner;     // its entry in g_stop_watches (never erased, so it stays put)
};

// What a watcher is told: a mod stopped, or a removed mod's data was forgotten.
struct Told {
    std::int32_t kind;  // LUMA_MODS_EVENT_STOPPED or LUMA_MODS_EVENT_FORGOTTEN
    std::string id;
};

// A running mod watching others stop and be forgotten: what happened since its last delivery, in order.
struct StopWatch {
    const LumaMod* watcher = nullptr;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::vector<Told> stopped;
    StopNode node{};
};

std::vector<std::unique_ptr<StopWatch>> g_stop_watches;  // g_options_lock

void deliver_stopped(LumaEventNode* node) {
    StopWatch* const owner = reinterpret_cast<StopNode*>(node)->owner;
    const LumaMod* mod = nullptr;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    std::vector<Told> stopped;
    {
        const std::lock_guard<std::mutex> lock(g_options_lock);
        if (owner->fn == nullptr) {
            return;
        }
        mod = owner->watcher;
        fn = owner->fn;
        user = owner->user;
        stopped.swap(owner->stopped);
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    for (const Told& told : stopped) {
        LumaEvent event{};
        event.size = sizeof(event);
        event.kind = told.kind;
        event.status = LUMA_OK;
        event.data = told.id.c_str();
        event.data_size = told.id.size();
        if (kernel == nullptr || kernel->call(mod, LUMA_MODS_NAME, fn, user, &event) != LUMA_OK) {
            return;  // the watcher is gone
        }
    }
}

void drop_stopped(LumaEventNode*) {}  // the node is the StopWatch's

std::int32_t mods_watch_mods(const LumaMod* mod, LumaEventFn fn, void* user) {
    if (const std::int32_t status = enter(mod, "mods.watch_mods"); status != LUMA_OK) {
        return status;
    }
    try {
        const std::lock_guard<std::mutex> lock(g_options_lock);
        StopWatch* watch = nullptr;
        StopWatch* free_one = nullptr;
        for (const std::unique_ptr<StopWatch>& one : g_stop_watches) {
            if (one->watcher == mod) {
                watch = one.get();
            } else if (one->watcher == nullptr && one->node.event.state == 0u && free_one == nullptr) {
                free_one = one.get();  // a gone mod's, not queued: used again
            }
        }
        if (watch == nullptr && fn == nullptr) {
            return LUMA_OK;
        }
        if (watch == nullptr) {
            if (free_one == nullptr) {
                g_stop_watches.push_back(std::make_unique<StopWatch>());
                free_one = g_stop_watches.back().get();
                free_one->node.owner = free_one;
                free_one->node.event.deliver = deliver_stopped;
                free_one->node.event.drop = drop_stopped;
            }
            watch = free_one;
            watch->watcher = mod;
            watch->node.event.mod = mod;
            watch->stopped.clear();
        }
        watch->fn = fn;
        watch->user = fn != nullptr ? user : nullptr;
        if (fn == nullptr) {
            watch->watcher = nullptr;
            watch->stopped.clear();
        }
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

// Under g_options_lock, any thread: every running mod watching (watch_mods) is told `id` with `kind`, at its next frame.
void tell_watchers_locked(const LumaServiceKernel* kernel, std::int32_t kind, std::string_view id) {
    for (const std::unique_ptr<StopWatch>& watch : g_stop_watches) {
        if (watch->fn == nullptr || watch->watcher == nullptr || kernel->mod_live(watch->watcher) == 0) {
            continue;
        }
        try {
            watch->stopped.push_back({kind, std::string(id)});
        } catch (...) {
            continue;  // no memory: this watcher misses the event
        }
        (void)kernel->post(&watch->node.event);  // queued already: the new one goes with it
    }
}

// Game thread, after the mod's stop: no event for it from now on (a node already queued is dropped by the kernel); every
// other running mod watching (watch_mods) is told its id.
void options_mod_gone(void*, const LumaMod* mod) {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const char* const gone_id = kernel != nullptr ? kernel->mod_id(mod) : nullptr;
    const std::lock_guard<std::mutex> lock(g_options_lock);
    for (auto& [id, options] : g_options) {
        if (options.watcher == mod) {
            options.watcher = nullptr;
            options.fn = nullptr;
            options.user = nullptr;
            options.changed.clear();
        }
    }
    for (const std::unique_ptr<StopWatch>& watch : g_stop_watches) {
        if (watch->watcher == mod) {
            watch->watcher = nullptr;
            watch->fn = nullptr;
            watch->user = nullptr;
            watch->stopped.clear();
        }
    }
    if (gone_id != nullptr) {
        tell_watchers_locked(kernel, LUMA_MODS_EVENT_STOPPED, gone_id);
    }
}

// ---- mods: kit data (section 9, lumakit R-6) ------------------------------------------------------------------------

// What the kit calls read of a mod in the session's list: its folder (an installed .luma's, or a folder mod's: both keep
// kit/<module>/<name>.json at their top) and the ids its luma.json needs.
struct KitMod {
    std::filesystem::path folder;
    std::vector<std::string> needs;
};

// Game thread. The index by mod id, made at the first kit call and again after any mod stops (a reload may replace a
// mod's files); the mods with code that began, in begin order (mod_begin), each live until its mod_gone; the bytes read,
// by mod and "<module>/<name>", dropped at that mod's mod_gone.
std::unordered_map<std::string, KitMod> g_kit_index;
bool g_kit_index_made = false;
std::vector<const LumaMod*> g_kit_running;
std::map<const LumaMod*, std::map<std::string, std::vector<unsigned char>>> g_kit_bytes;

void make_kit_index() {
    if (g_kit_index_made) {
        return;
    }
    g_kit_index_made = true;
    for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
        KitMod entry;
        entry.folder = mod.path;
        if (mod.luma) {
            for (const galaxy::luma::Dependency& need : mod.luma->needs) {
                entry.needs.push_back(need.id);
            }
        }
        g_kit_index[mod.id] = std::move(entry);
    }
}

// A module or a file's name: 1 to 64 of a-z 0-9 _ - ., not starting with a dot, no "..": no path part can be in it.
bool valid_kit_part(const char* part) noexcept {
    if (part == nullptr) {
        return false;
    }
    const std::string_view text(part);
    if (text.empty() || text.size() > 64u || text.front() == '.' || text.find("..") != std::string_view::npos) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

// The mod `of` names when `mod` may read its kit data: `of` is live, and it is `mod` itself or needs it. Otherwise null,
// and Mods.KitNotNeeded is reported (once, the kernel counting repeats).
const KitMod* kit_mod_for(const LumaMod* mod, const char* where, std::uint64_t of, const LumaMod** of_mod) {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const auto* const other = reinterpret_cast<const LumaMod*>(static_cast<std::uintptr_t>(of));
    const char* const own_id = kernel->mod_id(mod);
    // mod_id only compares the pointer with the kernel's records: a number that is no mod is simply unknown.
    const char* const other_id = of != 0u ? kernel->mod_id(other) : nullptr;
    if (other_id != nullptr && kernel->mod_live(other) != 0) {
        make_kit_index();
        if (const auto it = g_kit_index.find(other_id); it != g_kit_index.end()) {
            const KitMod& entry = it->second;
            if (other == mod || std::find(entry.needs.begin(), entry.needs.end(), own_id) != entry.needs.end()) {
                *of_mod = other;
                return &entry;
            }
        }
    }
    report(mod, where, 168u, other_id != nullptr ? other_id : "(no running mod)");  // Mods.KitNotNeeded
    return nullptr;
}

std::int32_t kit_names(const LumaMod* mod, std::uint64_t of, const char* module, char* buf, std::uint64_t size,
                       std::uint64_t* length) {
    static constexpr const char* where = "mods.kit_names";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (!valid_kit_part(module)) {
        return argument(mod, where, "module");
    }
    if (length == nullptr) {
        return argument(mod, where, "length");
    }
    if (buf == nullptr && size != 0u) {
        return argument(mod, where, "buf");
    }
    *length = 0u;
    const LumaMod* other = nullptr;
    const KitMod* const entry = kit_mod_for(mod, where, of, &other);
    if (entry == nullptr) {
        return LUMA_E_DENIED;
    }
    std::vector<std::string> names;
    if (!entry->folder.empty()) {
        std::error_code error;
        const std::filesystem::path folder = entry->folder / "kit" / module;
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const std::filesystem::path& file = it->path();
            if (!it->is_regular_file(error) || file.extension() != ".json") {
                continue;
            }
            const std::string stem = file.stem().string();
            if (valid_kit_part(stem.c_str())) {
                names.push_back(stem);  // a name kit_read cannot take is not listed
            }
        }
    }
    std::sort(names.begin(), names.end());
    std::uint64_t total = 0u;
    for (const std::string& name : names) {
        total += name.size() + 1u;
    }
    *length = total;
    if (buf == nullptr) {
        return LUMA_OK;  // the length asked
    }
    if (size < total) {
        return LUMA_E_TOO_LARGE;  // an answer: nothing written, *length is what it needs
    }
    for (const std::string& name : names) {
        std::memcpy(buf, name.c_str(), name.size() + 1u);
        buf += name.size() + 1u;
    }
    return LUMA_OK;
}

std::int32_t kit_read(const LumaMod* mod, std::uint64_t of, const char* module, const char* name, void* buf,
                      std::uint64_t size, std::uint64_t* length) {
    static constexpr const char* where = "mods.kit_read";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (!valid_kit_part(module)) {
        return argument(mod, where, "module");
    }
    if (!valid_kit_part(name)) {
        return argument(mod, where, "name");
    }
    if (length == nullptr) {
        return argument(mod, where, "length");
    }
    if (buf == nullptr && size != 0u) {
        return argument(mod, where, "buf");
    }
    *length = 0u;
    const LumaMod* other = nullptr;
    const KitMod* const entry = kit_mod_for(mod, where, of, &other);
    if (entry == nullptr) {
        return LUMA_E_DENIED;
    }
    const std::string key = std::string(module) + "/" + name;
    std::map<std::string, std::vector<unsigned char>>& kept = g_kit_bytes[other];
    auto it = kept.find(key);
    if (it == kept.end()) {
        if (entry->folder.empty()) {
            return LUMA_E_NOT_FOUND;
        }
        const std::filesystem::path file = entry->folder / "kit" / module / (std::string(name) + ".json");
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error)) {
            return LUMA_E_NOT_FOUND;  // an answer, not reported
        }
        const std::uintmax_t bytes = std::filesystem::file_size(file, error);
        if (error) {
            return LUMA_E_NOT_FOUND;
        }
        if (bytes > LUMA_MODS_KIT_FILE_MOST) {
            char found[24];
            std::snprintf(found, sizeof(found), "%llu", static_cast<unsigned long long>(bytes));
            report(mod, where, 169u, key.c_str(), found);  // Mods.KitTooLarge
            return LUMA_E_FORMAT;
        }
        std::vector<unsigned char> data(static_cast<std::size_t>(bytes));
        std::ifstream in(file, std::ios::binary);
        if (!in || (bytes != 0u && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(bytes)))) {
            return LUMA_E_IO;
        }
        it = kept.emplace(key, std::move(data)).first;
    }
    const std::vector<unsigned char>& data = it->second;
    *length = data.size();
    if (buf == nullptr) {
        return LUMA_OK;  // the length asked
    }
    if (size < data.size()) {
        return LUMA_E_TOO_LARGE;  // an answer: nothing written, *length is what it needs
    }
    if (!data.empty()) {
        std::memcpy(buf, data.data(), data.size());
    }
    return LUMA_OK;
}

std::int32_t kit_needing(const LumaMod* mod, std::uint64_t* out, std::uint32_t capacity, std::uint32_t* count) {
    static constexpr const char* where = "mods.needing";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (count == nullptr) {
        return argument(mod, where, "count");
    }
    if (out == nullptr && capacity != 0u) {
        return argument(mod, where, "out");
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const char* const own_id = kernel->mod_id(mod);
    make_kit_index();
    std::vector<std::pair<std::int32_t, const LumaMod*>> found;
    for (const LumaMod* other : g_kit_running) {
        const char* const other_id = kernel->mod_id(other);
        if (other == mod || other_id == nullptr || kernel->mod_live(other) == 0) {
            continue;
        }
        const auto it = g_kit_index.find(other_id);
        if (it != g_kit_index.end() &&
            std::find(it->second.needs.begin(), it->second.needs.end(), own_id) != it->second.needs.end()) {
            found.emplace_back(kernel->mod_order(other), other);
        }
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });  // the Mods list's order
    *count = static_cast<std::uint32_t>(found.size());
    for (std::uint32_t i = 0; i < capacity && i < found.size(); i++) {
        out[i] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(found[i].second));
    }
    return LUMA_OK;
}

// Game thread, before the mod's begin: a mod that may need another (every mod with code begins here).
// A reload begins a taken-out mod again: its line reads RUNNING again.
void kit_mod_begin(void*, const LumaMod* mod) {
    g_kit_running.push_back(mod);
    const LumaServiceKernel* const kernel = g_kernel.load(std::memory_order_acquire);
    if (const char* const id = kernel != nullptr ? kernel->mod_id(mod) : nullptr) {
        g_taken_out.erase(std::remove(g_taken_out.begin(), g_taken_out.end(), std::string_view(id)), g_taken_out.end());
        follow_take_out(id, false);
    }
}

// Game thread, after the mod's stop: it is no longer running, its bytes go, and the index is made again at the next
// kit call (a reload may have replaced its files).
void kit_mod_gone(const LumaMod* mod) {
    g_kit_running.erase(std::remove(g_kit_running.begin(), g_kit_running.end(), mod), g_kit_running.end());
    g_kit_bytes.erase(mod);
    g_kit_index.clear();
    g_kit_index_made = false;
}

// Every mod whose services began, when it is taken out (the kernel calls every provider's mod_gone; the id is valid
// until its mod_end): its line reads TAKEN_OUT.
void mods_mod_gone(void* user, const LumaMod* mod) {
    options_mod_gone(user, mod);
    kit_mod_gone(mod);
    const LumaServiceKernel* const kernel = g_kernel.load(std::memory_order_acquire);
    if (const char* const id = kernel != nullptr ? kernel->mod_id(mod) : nullptr) {
        if (!taken_out(id)) g_taken_out.emplace_back(id);
        follow_take_out(id, true);
    }
}

// ---- The providers -------------------------------------------------------------------------------------------------

// Set once at the host's start, before any mod or page asks (set_save_half, set_chunk_half).
SaveHalf g_save;
LumaSaveKept g_chunk_kept = nullptr;
LumaSaveForget g_chunk_forget = nullptr;

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

// The mod a caller() number names, by its id in the session's list. mod_id compares the number with the kernel's
// records only, so a number that is no mod is unknown, never read through.
std::int32_t mods_of_mod(const LumaMod* mod, std::uint64_t which, LumaModsInfo* out) {
    const char* const where = "mods.of_mod";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (out->size < kInfoLeast) {
        return struct_size(mod, where, out->size);
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const auto* const other = reinterpret_cast<const LumaMod*>(static_cast<std::uintptr_t>(which));
    const char* const id = which != 0u ? kernel->mod_id(other) : nullptr;
    if (id == nullptr || kernel->mod_live(other) == 0) {
        return LUMA_E_NOT_FOUND;
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

const LumaModsService kMods = {
    {sizeof(LumaModsService), LUMA_MODS_MAJOR, LUMA_MODS_MINOR},
    mods_count,
    mods_at,
    mods_find,
    mods_option,
    mods_watch_options,
    mods_watch_mods,
    kit_names,
    kit_read,
    kit_needing,
    mods_of_mod,
};

// mods' report rows (section 5): {where} first, then the row's own.
const LumaServiceRow kModsRows[] = {
    {165u, "Mods.OptionUnknown", {"where", "key", nullptr, nullptr}},
    {166u, "Mods.OptionNotOwn", {"where", "key", nullptr, nullptr}},
    {167u, "Mods.OptionBufferShort", {"where", "size", "key", "needed"}},
    {168u, "Mods.KitNotNeeded", {"where", "other", nullptr, nullptr}},
    {169u, "Mods.KitTooLarge", {"where", "file", "size", nullptr}},
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

// mods keeps a watcher per mod (section 8) and the kit bytes read of it (section 9), dropped at its stop, and which mods
// with code run (section 9's needing); the list is the session's, the record everyone's.
const LumaServiceProvider kModsProvider = {
    sizeof(LumaServiceProvider), LUMA_MODS_NAME, &kMods.base, nullptr, kModsRows,
    static_cast<std::uint32_t>(sizeof(kModsRows) / sizeof(kModsRows[0])), nullptr,
    kit_mod_begin, mods_mod_gone, nullptr, registered,
};
const LumaServiceProvider kIdsProvider = {
    sizeof(LumaServiceProvider), LUMA_MODS_IDS_NAME, &kIds.base, nullptr, nullptr, 0u, nullptr,
    nullptr, nullptr, nullptr, registered,
};
const LumaServiceProvider kInstallProvider = {
    sizeof(LumaServiceProvider), LUMA_MODS_INSTALL_NAME, &kInstall.base, nullptr, nullptr, 0u, nullptr,
    nullptr, install_mod_gone, nullptr, registered,
};
const LumaServiceProvider* const kProviders[] = {&kModsProvider, &kIdsProvider, &kInstallProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept {
    g_log = log;
    return kProviders;
}

void options_applied(const galaxy::mods::ModInfo& mod) {
    try {
        std::vector<std::pair<std::string, std::string>> now = texts_of(mod);
        const std::lock_guard<std::mutex> lock(g_options_lock);
        make_options_locked();
        const auto it = g_options.find(std::string_view(mod.id));
        if (it == g_options.end()) {
            return;  // not in this session's list (or no options): it reads them at its next start
        }
        ModOptions& options = it->second;
        bool any = false;
        for (std::size_t place = 0; place < options.values.size() && place < now.size(); place++) {
            if (options.values[place].first != now[place].first || options.values[place].second == now[place].second) {
                continue;  // the same key in the same place; an update that moved options waits for the next start
            }
            options.values[place].second = std::move(now[place].second);
            const std::uint32_t at = static_cast<std::uint32_t>(place);
            if (options.fn != nullptr &&
                std::find(options.changed.begin(), options.changed.end(), at) == options.changed.end()) {
                options.changed.push_back(at);
                any = true;
            }
        }
        if (!any) {
            return;
        }
        std::sort(options.changed.begin(), options.changed.end());  // luma.json order (header)
        if (!options.node) {
            options.node = std::make_unique<WatchNode>();
            options.node->owner = &options;
            options.node->event.deliver = deliver_options;
            options.node->event.drop = drop_options;
        }
        options.node->event.mod = options.watcher;
        if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
            (void)kernel->post(&options.node->event);  // CANCELLED: the mod is going; mod_gone clears it
        }
    } catch (...) {
        // Memory: the values stay as they were; the next Apply compares again.
    }
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
    // Everything a removed mod keeps (services/mods.md 8): ids with numbers, and ids the save halves keep values for.
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
    // And ids with a data folder of fs (services/fs.md 3).
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
        // Every running mod watching (watch_mods) is told, so a library drops what it holds for the id elsewhere.
        if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
            const std::lock_guard<std::mutex> lock(g_options_lock);
            tell_watchers_locked(kernel, LUMA_MODS_EVENT_FORGOTTEN, id);
        }
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

void flush_ids(unsigned wait_ms) noexcept {
    Record& r = record();
    std::unique_lock<std::mutex> lock(r.lock);
    if (!r.writer || r.written == r.changes) {
        return;
    }
    if (!r.caught_up.wait_for(lock, std::chrono::milliseconds(wait_ms), [&r] { return r.written == r.changes; })) {
        lock.unlock();
        log_line("LumaCore mods.ids: the record of mods' numbers was not written before the end; this session's new "
                 "numbers are derived again next time");
    }
}

void set_chunk_half(LumaSaveKept kept_ids, LumaSaveForget forget) noexcept {
    g_chunk_kept = kept_ids;
    g_chunk_forget = forget;
}

}  // namespace galaxy::luma_mods
