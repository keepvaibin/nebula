// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's file services (contract services-fs): `fs` (the mod's data folder and its own installed files) and
// `fs.exchange` (the Exchange folder, permission fs.exchange). A request is checked on the game thread, done on one
// worker thread in the order made (started at the first request, registry 10), and answered at a frame's start.

#include "galaxy/luma_fs_service.h"

#include "galaxy/atomic_file.h"
#include "galaxy/mods.h"
#include "galaxy/path_text.h"
#include "galaxy/platform_paths.h"

#include <luma_fs.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace galaxy::luma_fs {
namespace {

namespace stdfs = std::filesystem;

constexpr std::size_t kPathMost = 255u;              // bytes of a path (contract 4)
constexpr std::size_t kPartsMost = 16u;              // parts of a path
constexpr std::size_t kPartMost = 64u;               // bytes of a part
constexpr std::size_t kIdMost = 64u;                 // a .luma's id (luma.cpp valid_id)
constexpr std::uint64_t kFileMost = LUMA_FS_FILE_MOST;
constexpr std::uint64_t kDataSpace = 256ull << 20u;  // a mod's data folder (contract 5)
constexpr std::uint64_t kExchangeSpace = 1ull << 30u; // the Exchange folder
constexpr std::uint64_t kListBytesMost = 1ull << 20u;
constexpr std::size_t kListEntriesMost = 65536u;
constexpr std::uint32_t kRequestsMost = LUMA_FS_REQUESTS_MOST;

const char* const kFsName = LUMA_FS_NAME;
const char* const kExchangeName = LUMA_FS_EXCHANGE_NAME;

std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
void (*g_log)(const char*) = nullptr;  // set by providers() before the first mod

// ---- The path rule (contract 4) ------------------------------------------------------------------------------------

char fold(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_name(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i++) {
        if (fold(a[i]) != fold(b[i])) {
            return false;
        }
    }
    return true;
}

bool part_in_rule(std::string_view part) noexcept {
    if (part.empty() || part.size() > kPartMost) {
        return false;
    }
    if (part.front() == ' ' || part.front() == '.' || part.back() == ' ' || part.back() == '.') {
        return false;
    }
    for (const char c : part) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != ' ' && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    // The names Windows keeps for devices, alone or before a '.', in any case.
    const std::string_view stem = part.substr(0, part.find('.'));
    if (same_name(stem, "con") || same_name(stem, "prn") || same_name(stem, "aux") || same_name(stem, "nul")) {
        return false;
    }
    if (stem.size() == 4u && (same_name(stem.substr(0, 3), "com") || same_name(stem.substr(0, 3), "lpt")) &&
        stem[3] >= '1' && stem[3] <= '9') {
        return false;
    }
    // The game's half-written files (atomic_file.cpp): "<name>.tmp.<process>.<sequence>".
    if (const std::size_t at = part.find(".tmp."); at != std::string_view::npos) {
        const std::string_view tail = part.substr(at + 5u);
        if (!tail.empty() && std::all_of(tail.begin(), tail.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
            return false;
        }
    }
    return true;
}

// The parts of a path in the rule into *parts (cleared first); false when it breaks it. "" gives no parts, allowed
// only when `empty_ok`.
bool split_path(std::string_view path, bool empty_ok, std::vector<std::string>* parts) {
    parts->clear();
    if (path.empty()) {
        return empty_ok;
    }
    if (path.size() > kPathMost) {
        return false;
    }
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = path.find('/', start);
        const std::string_view part = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (!part_in_rule(part) || parts->size() == kPartsMost) {
            parts->clear();
            return false;
        }
        parts->emplace_back(part);
        if (slash == std::string_view::npos) {
            return true;
        }
        start = slash + 1u;
    }
}

bool valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > kIdMost) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

// ---- The disk (worker thread) --------------------------------------------------------------------------------------

std::string name_of(const stdfs::path& path) {
    return galaxy::path_text(path.filename());
}

// A plain file or a plain folder: never a link, a junction or anything else the system has.
enum class Kind { Missing, File, Folder, Other };

Kind kind_of(const stdfs::path& path) noexcept {
    std::error_code error;
    const stdfs::file_status status = stdfs::symlink_status(path, error);
    if (error || status.type() == stdfs::file_type::not_found) {
        return Kind::Missing;
    }
    switch (status.type()) {
    case stdfs::file_type::regular:
        return Kind::File;
    case stdfs::file_type::directory:
        return Kind::Folder;
    default:
        return Kind::Other;  // a link, a junction (MSVC's file_type::junction), a device, a pipe
    }
}

// The folder's entry named `name` up to letter case, as the folder spells it; empty when there is none. Names in the
// rule are ASCII, so folding ASCII letters is the whole comparison.
stdfs::path scan_child(const stdfs::path& folder, const std::string& name) {
    std::error_code error;
    for (stdfs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (same_name(name_of(it->path()), name)) {
            return it->path();
        }
    }
    return {};
}

// The child of `folder` named `name` up to letter case (contract 4): the exact name first, else, where the system's
// names keep case (Linux, Android, a case-sensitive Apple volume), the folder's entry of that name up to case. On
// Windows a miss of the exact name is a miss of every case. `spelled`: as the folder spells it (a scan where the system
// ignores case), for a write that keeps the file's case. Empty when there is none.
stdfs::path find_child(const stdfs::path& folder, const std::string& name, bool spelled) {
    stdfs::path exact = folder / galaxy::text_path(name);
    if (kind_of(exact) != Kind::Missing) {
#if defined(_WIN32) || defined(__APPLE__)
        if (spelled) {
            if (stdfs::path found = scan_child(folder, name); !found.empty()) {
                return found;
            }
        }
#endif
        return exact;
    }
#if defined(_WIN32)
    return {};
#else
    return scan_child(folder, name);
#endif
}

struct Place {
    std::int32_t status = LUMA_OK;  // LUMA_OK, LUMA_E_NOT_FOUND, LUMA_E_DENIED, LUMA_E_IO
    stdfs::path path;               // where the path is (its existing case), or where it would be made
    Kind kind = Kind::Missing;
};

// Walks `parts` from `root` without following anything but plain folders. A missing last part is a Place of kind
// Missing at the name given; a missing folder before it is NOT_FOUND unless `make` (then it is made).
Place resolve(const stdfs::path& root, const std::vector<std::string>& parts, bool make) {
    Place place;
    place.path = root;
    place.kind = kind_of(root);
    if (place.kind == Kind::Missing && make) {
        std::error_code error;
        stdfs::create_directories(root, error);
        place.kind = kind_of(root);
    }
    if (place.kind != Kind::Folder) {
        place.status = place.kind == Kind::Missing ? LUMA_E_NOT_FOUND : LUMA_E_DENIED;
        return place;
    }
    for (std::size_t i = 0; i < parts.size(); i++) {
        const bool last = i + 1u == parts.size();
        stdfs::path next = find_child(place.path, parts[i], make && last);
        if (next.empty()) {
            next = place.path / galaxy::text_path(parts[i]);
            if (last) {
                place.path = std::move(next);
                place.kind = Kind::Missing;
                return place;
            }
            if (!make) {
                place.status = LUMA_E_NOT_FOUND;
                return place;
            }
            std::error_code error;
            stdfs::create_directory(next, error);
            if (error && kind_of(next) != Kind::Folder) {
                place.status = LUMA_E_IO;
                return place;
            }
        }
        const Kind kind = kind_of(next);
        if (kind == Kind::Other) {
            place.status = LUMA_E_DENIED;
            return place;
        }
        if (!last && kind != Kind::Folder) {
            place.status = LUMA_E_NOT_FOUND;  // a file where a folder of the path would be
            return place;
        }
        place.path = std::move(next);
        place.kind = kind;
    }
    return place;
}

// The bytes of the plain files under `folder` (links and junctions not followed, not counted).
std::uint64_t folder_bytes(const stdfs::path& folder) {
    std::uint64_t total = 0;
    std::error_code error;
    for (stdfs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const Kind kind = kind_of(it->path());
        if (kind == Kind::File) {
            std::error_code size_error;
            const std::uint64_t size = stdfs::file_size(it->path(), size_error);
            total += size_error ? 0u : size;
        } else if (kind == Kind::Folder) {
            total += folder_bytes(it->path());
        }
    }
    return total;
}

// Removes `path` and, for a plain folder, everything in it; a link or junction inside is removed as itself, never
// followed. Counts the files removed into *files. False when anything stayed.
bool remove_tree(const stdfs::path& path, Kind kind, std::uint64_t* files) {
    bool ok = true;
    if (kind == Kind::Folder) {
        std::vector<stdfs::path> children;
        std::error_code error;
        for (stdfs::directory_iterator it(path, error), end; !error && it != end; it.increment(error)) {
            children.push_back(it->path());
        }
        if (error) {
            return false;
        }
        for (const stdfs::path& child : children) {
            ok = remove_tree(child, kind_of(child), files) && ok;
        }
    }
    // A file gone meanwhile (the player, another mod's request) is not a failure: it is not there either way.
    std::error_code error;
    const bool removed = stdfs::remove(path, error);
    if (error) {
        return false;
    }
    if (removed && kind == Kind::File) {
        (*files)++;
    }
    return ok;
}

std::int32_t read_file(const stdfs::path& path, std::string* out) {
    std::error_code error;
    const std::uint64_t size = stdfs::file_size(path, error);
    if (error) {
        return LUMA_E_IO;
    }
    if (size > kFileMost) {
        return LUMA_E_TOO_LARGE;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return LUMA_E_DENIED;  // the system refused to open it (contract 6)
    }
    try {
        out->resize(static_cast<std::size_t>(size));
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    file.read(out->data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(file.gcount()) != size || file.bad()) {
        out->clear();
        return LUMA_E_IO;
    }
    // A file that grew meanwhile is read as it was when measured; one that shrank is IO above.
    return LUMA_OK;
}

// The list's lines under `folder` (prefix: the lines' path so far), depth-first; `parts` counts the path's parts so
// far with the asked path's. LUMA_OK or LUMA_E_TOO_LARGE.
std::int32_t list_into(const stdfs::path& folder, const std::string& prefix, std::size_t parts, std::size_t path_bytes,
                       bool recursive, std::vector<std::string>* lines, std::uint64_t* bytes) {
    std::vector<std::pair<std::string, Kind>> entries;
    std::error_code error;
    for (stdfs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        std::string name = name_of(it->path());
        const Kind kind = kind_of(it->path());
        // Only what a path can name: plain files and folders in the rule, within its parts and bytes.
        if ((kind != Kind::File && kind != Kind::Folder) || !part_in_rule(name) || parts + 1u > kPartsMost ||
            path_bytes + (path_bytes == 0u ? 0u : 1u) + name.size() > kPathMost) {
            continue;
        }
        entries.emplace_back(std::move(name), kind);
    }
    std::sort(entries.begin(), entries.end());
    for (const auto& [name, kind] : entries) {
        std::string line = prefix + name;
        if (kind == Kind::Folder) {
            line += '/';
        }
        *bytes += line.size() + 1u;
        lines->push_back(line);
        if (lines->size() > kListEntriesMost || *bytes > kListBytesMost) {
            return LUMA_E_TOO_LARGE;
        }
        if (recursive && kind == Kind::Folder) {
            const std::size_t child_bytes = path_bytes + (path_bytes == 0u ? 0u : 1u) + name.size();
            if (const std::int32_t status = list_into(folder / galaxy::text_path(name), line, parts + 1u, child_bytes, true,
                                                      lines, bytes);
                status != LUMA_OK) {
                return status;
            }
        }
    }
    return LUMA_OK;
}

// ---- Requests ------------------------------------------------------------------------------------------------------

enum class Root : std::uint8_t { Data, Package, Exchange };
enum class Op : std::uint8_t { Read, Write, Remove, Move, List, Forget };

struct Request;
struct ModState;

struct Node {
    LumaEventNode event;
    Request* request;
};

struct Request {
    Node node{};
    ModState* owner = nullptr;  // g_lock; null once the mod is gone (then nothing is posted or called for it)
    Request* next = nullptr;    // g_lock: the owner's list
    bool cancelled = false;     // g_lock: the mod cancelled it (no event)
    bool taken = false;         // game thread, in the event's call
    bool exchange = false;      // asked through fs.exchange (the event's service name)
    Root root = Root::Data;
    Op op = Op::Read;
    bool recursive = false;
    std::uint32_t kind = 0;
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    stdfs::path folder;  // the root's folder, set on the game thread
    std::vector<std::string> parts;
    std::vector<std::string> to_parts;  // move's
    // In: write's bytes. Out: read's bytes, list's text. Written by the worker before it posts, read on the game
    // thread after.
    std::string bytes;
    std::int32_t status = LUMA_OK;
    std::uint64_t value = 0;
};

struct ModState {
    const LumaMod* mod = nullptr;
    std::string id;
    stdfs::path package;        // game thread: the mod's folder in the Mods folder, found at its first package request
    bool package_looked = false; // game thread
    std::uint32_t open = 0;     // g_lock: requests counted against kRequestsMost
    bool limit_said = false;    // game thread: LUMA_E_LIMIT of the data folder said on the Mods page
    bool exchange_said = false; // likewise, the Exchange folder
    Request* first = nullptr;   // g_lock
};

std::mutex g_lock;
std::vector<std::unique_ptr<ModState>> g_mods;  // game thread
std::atomic<LumaHandle> g_next_handle{1};
Request* g_in_call = nullptr;  // game thread: the request whose event's call runs

// The worker: one thread, its queue taken in order (contract 6). Made at the first request and never freed, so a
// request still queued at the process's end meets no destroyed lock.
struct Worker {
    std::mutex lock;
    std::condition_variable ready;
    std::deque<Request*> jobs;
    bool started = false;
};
Worker* g_worker = nullptr;      // game thread makes it; its fields under its lock
std::mutex g_worker_make;        // forget_data may come from any thread

void answer(Request* request);
void work(Request* request);

Worker* worker() {
    const std::lock_guard<std::mutex> lock(g_worker_make);
    if (g_worker == nullptr) {
        g_worker = new (std::nothrow) Worker();
        if (g_worker == nullptr) {
            return nullptr;
        }
        try {
            std::thread([w = g_worker] {
                for (;;) {
                    Request* request = nullptr;
                    {
                        std::unique_lock<std::mutex> jobs_lock(w->lock);
                        w->ready.wait(jobs_lock, [w] { return !w->jobs.empty(); });
                        request = w->jobs.front();
                        w->jobs.pop_front();
                    }
                    work(request);
                }
            }).detach();
            g_worker->started = true;
        } catch (...) {
            if (g_log != nullptr) {
                g_log("LumaCore fs: the worker thread could not start; every request answers LUMA_E_IO");
            }
        }
    }
    return g_worker;
}

// Queues `request` for the worker; without one it is answered LUMA_E_IO at once.
void queue(Request* request) {
    Worker* w = worker();
    if (w == nullptr || !w->started) {
        request->status = LUMA_E_IO;
        answer(request);
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(w->lock);
        w->jobs.push_back(request);
    }
    w->ready.notify_one();
}

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr, const char* b = nullptr) noexcept {
    const char* args[2] = {a, b};
    const std::uint32_t count = a == nullptr ? 0u : b == nullptr ? 1u : 2u;
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, count);
    }
}

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

ModState* state_of(const LumaMod* mod) noexcept {
    for (const std::unique_ptr<ModState>& state : g_mods) {
        if (state->mod == mod) {
            return state.get();
        }
    }
    return nullptr;
}

void unlink(Request* request) noexcept {  // g_lock
    if (request->owner == nullptr) {
        return;
    }
    for (Request** at = &request->owner->first; *at != nullptr; at = &(*at)->next) {
        if (*at == request) {
            *at = request->next;
            break;
        }
    }
    request->next = nullptr;
    request->owner = nullptr;
}

void deliver(LumaEventNode* node);
void drop(LumaEventNode* node);

// The worker's answer (once): the event for the frame's start, or the request freed when its mod is gone.
void answer(Request* request) {
    const std::lock_guard<std::mutex> lock(g_lock);
    if (request->owner == nullptr) {
        delete request;  // the mod is gone: nothing is posted for it (luma_service_provider.h, mod_gone)
        return;
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr || kernel->post(&request->node.event) != LUMA_OK) {
        if (!request->cancelled && request->owner->open > 0u) {
            request->owner->open--;
        }
        unlink(request);
        delete request;
    }
}

// Game thread, at the frame's start: the event unless the request was cancelled or its mod is gone; then it is closed.
void deliver(LumaEventNode* node) {
    Request* const request = reinterpret_cast<Node*>(node)->request;
    bool call = false;
    ModState* owner = nullptr;  // game thread: live until mod_end, which comes after mod_gone unlinked everything
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        if (request->owner != nullptr) {
            owner = request->owner;
            call = !request->cancelled;
            if (call && owner->open > 0u) {
                owner->open--;
            }
            unlink(request);
        }
    }
    // A full folder is said on the Mods page once a session for each mod and folder (header, LUMA_E_LIMIT).
    if (call && request->status == LUMA_E_LIMIT) {
        bool& said = request->exchange ? owner->exchange_said : owner->limit_said;
        if (!said) {
            said = true;
            report(node->mod, request->exchange ? "fs.exchange.write" : "fs.write", 104u,
                   request->exchange ? "1 GiB" : "256 MiB");  // Service.Limit
        }
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (call && kernel != nullptr) {
        LumaEvent event{};
        event.size = sizeof(event);
        event.kind = request->kind;
        event.status = request->status;
        event.handle = request->handle;
        event.value = request->status == LUMA_OK ? request->value : 0u;
        g_in_call = request;
        kernel->call(node->mod, request->exchange ? kExchangeName : kFsName, request->fn, request->user, &event);
        g_in_call = nullptr;
    }
    delete request;
}

// Game thread: the mod is gone and the node was queued.
void drop(LumaEventNode* node) {
    Request* const request = reinterpret_cast<Node*>(node)->request;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        unlink(request);
    }
    delete request;
}

// ---- The worker's part ---------------------------------------------------------------------------------------------

std::int32_t do_read(Request& request) {
    const Place place = resolve(request.folder, request.parts, false);
    if (place.status != LUMA_OK) {
        return place.status;
    }
    if (place.kind != Kind::File) {
        return LUMA_E_NOT_FOUND;  // missing, or a folder
    }
    const std::int32_t status = read_file(place.path, &request.bytes);
    request.value = status == LUMA_OK ? request.bytes.size() : 0u;
    return status;
}

std::int32_t do_write(Request& request) {
    const std::uint64_t space = request.root == Root::Exchange ? kExchangeSpace : kDataSpace;
    // The folder's bytes now, the file's own old bytes not counted (it is replaced): the player may change the
    // folder between two writes, so it is measured each time.
    Place place = resolve(request.folder, request.parts, false);
    std::uint64_t used = kind_of(request.folder) == Kind::Folder ? folder_bytes(request.folder) : 0u;
    if (place.status == LUMA_OK && place.kind == Kind::File) {
        std::error_code error;
        const std::uint64_t old = stdfs::file_size(place.path, error);
        used -= error ? 0u : std::min(used, old);
    }
    if (used + request.bytes.size() > space) {
        return LUMA_E_LIMIT;
    }
    place = resolve(request.folder, request.parts, true);
    if (place.status != LUMA_OK) {
        return place.status;
    }
    if (place.kind == Kind::Folder) {
        return LUMA_E_EXISTS;
    }
    if (!galaxy::io::write_file_atomically(place.path, request.bytes)) {
        return LUMA_E_IO;
    }
    request.value = request.bytes.size();
    request.bytes.clear();
    request.bytes.shrink_to_fit();
    return LUMA_OK;
}

std::int32_t do_remove(Request& request) {
    const Place place = resolve(request.folder, request.parts, false);
    if (place.status != LUMA_OK) {
        // An empty data folder never made: nothing to remove ("" is the folder itself).
        return request.parts.empty() && place.status == LUMA_E_NOT_FOUND ? LUMA_OK : place.status;
    }
    if (place.kind == Kind::Missing) {
        return LUMA_E_NOT_FOUND;
    }
    std::uint64_t files = 0;
    bool ok = true;
    if (request.parts.empty()) {
        // "": the data folder's contents; the folder stays.
        std::vector<stdfs::path> children;
        std::error_code error;
        for (stdfs::directory_iterator it(place.path, error), end; !error && it != end; it.increment(error)) {
            children.push_back(it->path());
        }
        ok = !error;
        for (const stdfs::path& child : children) {
            ok = remove_tree(child, kind_of(child), &files) && ok;
        }
    } else {
        ok = remove_tree(place.path, place.kind, &files);
    }
    request.value = files;
    return ok ? LUMA_OK : LUMA_E_IO;
}

std::int32_t do_move(Request& request) {
    const Place from = resolve(request.folder, request.parts, false);
    if (from.status != LUMA_OK) {
        return from.status;
    }
    if (from.kind == Kind::Missing) {
        return LUMA_E_NOT_FOUND;
    }
    // Only the letter case changes: the same file, given the new case in place.
    bool same = request.parts.size() == request.to_parts.size();
    for (std::size_t i = 0; same && i < request.parts.size(); i++) {
        same = same_name(request.parts[i], request.to_parts[i]);
    }
    stdfs::path target;
    if (same) {
        target = from.path.parent_path() / galaxy::text_path(request.to_parts.back());
    } else {
        const Place to = resolve(request.folder, request.to_parts, true);
        if (to.status != LUMA_OK) {
            return to.status;
        }
        if (to.kind != Kind::Missing) {
            return LUMA_E_EXISTS;
        }
        target = to.path;
    }
    std::error_code error;
    stdfs::rename(from.path, target, error);
    return error ? LUMA_E_IO : LUMA_OK;
}

std::int32_t do_list(Request& request) {
    const Place place = resolve(request.folder, request.parts, false);
    if (place.status != LUMA_OK) {
        // A data folder never made lists empty; any other missing folder is NOT_FOUND.
        if (request.parts.empty() && place.status == LUMA_E_NOT_FOUND) {
            request.value = 0;
            return LUMA_OK;
        }
        return place.status;
    }
    if (place.kind != Kind::Folder) {
        return LUMA_E_NOT_FOUND;
    }
    std::size_t path_bytes = 0;
    for (const std::string& part : request.parts) {
        path_bytes += (path_bytes == 0u ? 0u : 1u) + part.size();
    }
    std::vector<std::string> lines;
    std::uint64_t bytes = 0;
    if (const std::int32_t status = list_into(place.path, {}, request.parts.size(), path_bytes, request.recursive,
                                              &lines, &bytes);
        status != LUMA_OK) {
        return status;
    }
    // Byte order over whole paths: a folder's line ("a/") before what it holds ("a/b"), since it is their start.
    std::sort(lines.begin(), lines.end());
    try {
        request.bytes.reserve(static_cast<std::size_t>(bytes));
        for (const std::string& line : lines) {
            request.bytes += line;
            request.bytes += '\n';
        }
    } catch (...) {
        request.bytes.clear();
        return LUMA_E_MEMORY;
    }
    request.value = request.bytes.size();
    return LUMA_OK;
}

void work(Request* request) {
    if (request->op == Op::Forget) {
        // The player's FORGET (no mod, no event): the folder goes after every request queued before.
        std::uint64_t files = 0;
        const Kind kind = kind_of(request->folder);
        if (kind == Kind::Folder) {
            remove_tree(request->folder, kind, &files);
        }
        delete request;
        return;
    }
    bool skip = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        // A read or list nobody waits for is not done; a write, remove or move keeps its place in the order whatever
        // happened to its mod (contract 6).
        skip = (request->owner == nullptr || request->cancelled) && (request->op == Op::Read || request->op == Op::List);
    }
    std::int32_t status = LUMA_E_CANCELLED;
    if (!skip) {
        try {
            switch (request->op) {
            case Op::Read:
                status = do_read(*request);
                break;
            case Op::Write:
                status = do_write(*request);
                break;
            case Op::Remove:
                status = do_remove(*request);
                break;
            case Op::Move:
                status = do_move(*request);
                break;
            case Op::List:
                status = do_list(*request);
                break;
            case Op::Forget:
                break;
            }
        } catch (...) {
            status = LUMA_E_MEMORY;  // std::filesystem's paths allocate
        }
    }
    request->status = status;
    if (status != LUMA_OK) {
        request->bytes.clear();
        request->bytes.shrink_to_fit();
        request->value = 0;
    }
    answer(request);
}

// ---- The functions (game thread) -----------------------------------------------------------------------------------

stdfs::path data_root() {
    const stdfs::path base = galaxy::nebula_data_directory();
    return base.empty() ? stdfs::path() : base / "mod-data";
}

// The checks every request shares: the mod, fn and out, the permission's grant (fs.exchange is bound only when
// granted, so this holds), the limit. Null: the status in *status.
ModState* request_enter(const LumaMod* mod, const char* where, LumaEventFn fn, LumaHandle* out, std::int32_t* status) {
    *status = enter(mod, where);
    if (*status != LUMA_OK) {
        return nullptr;
    }
    if (fn == nullptr) {
        *status = argument(mod, where, "fn");
        return nullptr;
    }
    if (out == nullptr) {
        *status = argument(mod, where, "out");
        return nullptr;
    }
    ModState* state = state_of(mod);
    if (state == nullptr) {
        *status = LUMA_E_CANCELLED;
        return nullptr;
    }
    std::uint32_t open = 0;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        open = state->open;
    }
    if (open >= kRequestsMost) {
        report(mod, where, 104u, "32");  // Service.Limit
        *status = LUMA_E_LIMIT;
        return nullptr;
    }
    return state;
}

// A path argument's parts; LUMA_OK, or LUMA_E_ARGUMENT reported naming `name`.
std::int32_t path_argument(const LumaMod* mod, const char* where, const char* name, const char* path, bool empty_ok,
                           std::vector<std::string>* parts) {
    if (path == nullptr) {
        return argument(mod, where, name);
    }
    const std::size_t length = ::strnlen(path, kPathMost + 1u);
    if (length > kPathMost || !split_path(std::string_view(path, length), empty_ok, parts)) {
        parts->clear();
        return argument(mod, where, name);
    }
    return LUMA_OK;
}

// Makes, links and queues the request; LUMA_PENDING with its handle in *out, or LUMA_E_MEMORY.
std::int32_t start(ModState& state, const LumaMod* mod, Request* request, LumaEventFn fn, void* user, LumaHandle* out) {
    request->node.event.mod = mod;
    request->node.event.deliver = deliver;
    request->node.event.drop = drop;
    request->node.request = request;
    request->handle = g_next_handle.fetch_add(1u, std::memory_order_relaxed);
    request->fn = fn;
    request->user = user;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        request->owner = &state;
        request->next = state.first;
        state.first = request;
        state.open++;
    }
    *out = request->handle;
    queue(request);
    return LUMA_PENDING;
}

stdfs::path root_folder(ModState& state, Root root) {
    switch (root) {
    case Root::Data: {
        const stdfs::path base = data_root();
        return base.empty() || !valid_id(state.id) ? stdfs::path() : base / state.id;
    }
    case Root::Package:
        // Looked up at the first package request only: a copy of the session's list for each mod at its begin would
        // cost every mod, used or not.
        if (!state.package_looked) {
            state.package_looked = true;
            const galaxy::mods::SessionMods session = galaxy::mods::session_mods();
            for (const galaxy::mods::ModInfo& info : session.started_with) {
                if (info.id == state.id) {
                    state.package = info.path;
                    break;
                }
            }
        }
        return state.package;
    case Root::Exchange:
        return exchange_folder();
    }
    return {};
}

std::int32_t ask(const LumaMod* mod, const char* where, Root root, Op op, const char* path, const char* to,
                 const void* data, std::uint64_t bytes, std::uint32_t recursive, LumaEventFn fn, void* user,
                 LumaHandle* out) {
    std::int32_t status = LUMA_OK;
    ModState* state = request_enter(mod, where, fn, out, &status);
    if (state == nullptr) {
        return status;
    }
    std::unique_ptr<Request> request(new (std::nothrow) Request());
    if (request == nullptr) {
        return LUMA_E_MEMORY;
    }
    request->root = root;
    request->op = op;
    request->exchange = root == Root::Exchange;
    // "" is the folder itself for list and for fs's remove; the Exchange folder is the player's to empty.
    const bool empty_ok = op == Op::List || (op == Op::Remove && root == Root::Data);
    if (const std::int32_t checked = path_argument(mod, where, op == Op::Move ? "from" : "path", path, empty_ok,
                                                   &request->parts);
        checked != LUMA_OK) {
        return checked;
    }
    switch (op) {
    case Op::Read:
        request->kind = LUMA_FS_EVENT_READ;
        break;
    case Op::Write:
        if (data == nullptr && bytes > 0u) {
            return argument(mod, where, "data");
        }
        if (bytes > kFileMost) {
            char most[24];
            std::snprintf(most, sizeof(most), "%llu", static_cast<unsigned long long>(kFileMost));
            report(mod, where, 104u, most);  // Service.Limit: the file's bytes
            return LUMA_E_TOO_LARGE;
        }
        try {
            if (bytes > 0u) {  // a null data with 0 bytes is an empty file
                request->bytes.assign(static_cast<const char*>(data), static_cast<std::size_t>(bytes));
            }
        } catch (...) {
            return LUMA_E_MEMORY;
        }
        request->kind = LUMA_FS_EVENT_WRITE;
        break;
    case Op::Remove:
        request->kind = LUMA_FS_EVENT_REMOVE;
        break;
    case Op::Move: {
        if (const std::int32_t checked = path_argument(mod, where, "to", to, false, &request->to_parts);
            checked != LUMA_OK) {
            return checked;
        }
        // `to` inside `from` (up to case): a folder cannot hold itself.
        bool inside = request->to_parts.size() > request->parts.size();
        for (std::size_t i = 0; inside && i < request->parts.size(); i++) {
            inside = same_name(request->parts[i], request->to_parts[i]);
        }
        if (inside) {
            return argument(mod, where, "to");
        }
        request->kind = LUMA_FS_EVENT_MOVE;
        break;
    }
    case Op::List:
        if (recursive > 1u) {
            return argument(mod, where, "recursive");
        }
        request->recursive = recursive == 1u;
        request->kind = LUMA_FS_EVENT_LIST;
        break;
    case Op::Forget:
        return LUMA_E_ARGUMENT;
    }
    try {
        request->folder = root_folder(*state, root);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    if (request->folder.empty()) {
        return LUMA_E_ABSENT;  // no data folder on this platform, or a mod with no installed folder: an answer
    }
    return start(*state, mod, request.release(), fn, user, out);
}

// The take of a READ or LIST event's call.
std::int64_t take(const LumaMod* mod, const char* where, LumaHandle handle, std::uint32_t kind, void* buf,
                  std::uint64_t size) {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    Request* request = g_in_call;
    if (request == nullptr || request->handle != handle || request->node.event.mod != mod || request->kind != kind ||
        request->status != LUMA_OK || request->taken) {
        report(mod, where, 102u);  // Service.Handle
        return LUMA_E_HANDLE;
    }
    if (buf == nullptr && size > 0u) {
        return argument(mod, where, "buf");
    }
    const std::uint64_t length = request->bytes.size();
    if (buf == nullptr) {
        return static_cast<std::int64_t>(length);  // the length first (D12.4); not the take
    }
    const std::uint64_t copied = std::min(length, size);
    std::memcpy(buf, request->bytes.data(), static_cast<std::size_t>(copied));
    request->taken = true;
    return static_cast<std::int64_t>(length);
}

std::int32_t cancel_request(const LumaMod* mod, LumaHandle handle, bool exchange, const char* where) {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return LUMA_E_CANCELLED;
    }
    bool found = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        for (Request* request = state->first; request != nullptr; request = request->next) {
            if (request->handle == handle && request->exchange == exchange && !request->cancelled) {
                // It stays linked until the worker answers and the node is delivered: then it is freed, no call.
                request->cancelled = true;
                found = true;
                if (state->open > 0u) {
                    state->open--;
                }
                break;
            }
        }
    }
    if (!found) {
        report(mod, where, 102u);  // Service.Handle
        return LUMA_E_HANDLE;
    }
    return LUMA_OK;
}

// fs

std::int32_t fs_read(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out) {
    return ask(mod, "fs.read", Root::Data, Op::Read, path, nullptr, nullptr, 0u, 0u, fn, user, out);
}
std::int32_t fs_read_package(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out) {
    return ask(mod, "fs.read_package", Root::Package, Op::Read, path, nullptr, nullptr, 0u, 0u, fn, user, out);
}
std::int64_t fs_take_read(const LumaMod* mod, LumaHandle request, void* buf, std::uint64_t size) {
    return take(mod, "fs.take_read", request, LUMA_FS_EVENT_READ, buf, size);
}
std::int32_t fs_write(const LumaMod* mod, const char* path, const void* data, std::uint64_t bytes, LumaEventFn fn,
                      void* user, LumaHandle* out) {
    return ask(mod, "fs.write", Root::Data, Op::Write, path, nullptr, data, bytes, 0u, fn, user, out);
}
std::int32_t fs_remove(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out) {
    return ask(mod, "fs.remove", Root::Data, Op::Remove, path, nullptr, nullptr, 0u, 0u, fn, user, out);
}
std::int32_t fs_move(const LumaMod* mod, const char* from, const char* to, LumaEventFn fn, void* user, LumaHandle* out) {
    return ask(mod, "fs.move", Root::Data, Op::Move, from, to, nullptr, 0u, 0u, fn, user, out);
}
std::int32_t fs_list(const LumaMod* mod, const char* path, std::uint32_t recursive, LumaEventFn fn, void* user,
                     LumaHandle* out) {
    return ask(mod, "fs.list", Root::Data, Op::List, path, nullptr, nullptr, 0u, recursive, fn, user, out);
}
std::int32_t fs_list_package(const LumaMod* mod, const char* path, std::uint32_t recursive, LumaEventFn fn, void* user,
                             LumaHandle* out) {
    return ask(mod, "fs.list_package", Root::Package, Op::List, path, nullptr, nullptr, 0u, recursive, fn, user, out);
}
std::int64_t fs_take_list(const LumaMod* mod, LumaHandle request, char* buf, std::uint64_t size) {
    return take(mod, "fs.take_list", request, LUMA_FS_EVENT_LIST, buf, size);
}
std::int32_t fs_cancel(const LumaMod* mod, LumaHandle request) {
    return cancel_request(mod, request, false, "fs.cancel");
}

// fs.exchange

std::int32_t ex_read(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out) {
    return ask(mod, "fs.exchange.read", Root::Exchange, Op::Read, path, nullptr, nullptr, 0u, 0u, fn, user, out);
}
std::int64_t ex_take_read(const LumaMod* mod, LumaHandle request, void* buf, std::uint64_t size) {
    return take(mod, "fs.exchange.take_read", request, LUMA_FS_EVENT_READ, buf, size);
}
std::int32_t ex_write(const LumaMod* mod, const char* path, const void* data, std::uint64_t bytes, LumaEventFn fn,
                      void* user, LumaHandle* out) {
    return ask(mod, "fs.exchange.write", Root::Exchange, Op::Write, path, nullptr, data, bytes, 0u, fn, user, out);
}
std::int32_t ex_remove(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out) {
    return ask(mod, "fs.exchange.remove", Root::Exchange, Op::Remove, path, nullptr, nullptr, 0u, 0u, fn, user, out);
}
std::int32_t ex_move(const LumaMod* mod, const char* from, const char* to, LumaEventFn fn, void* user, LumaHandle* out) {
    return ask(mod, "fs.exchange.move", Root::Exchange, Op::Move, from, to, nullptr, 0u, 0u, fn, user, out);
}
std::int32_t ex_list(const LumaMod* mod, const char* path, std::uint32_t recursive, LumaEventFn fn, void* user,
                     LumaHandle* out) {
    return ask(mod, "fs.exchange.list", Root::Exchange, Op::List, path, nullptr, nullptr, 0u, recursive, fn, user, out);
}
std::int64_t ex_take_list(const LumaMod* mod, LumaHandle request, char* buf, std::uint64_t size) {
    return take(mod, "fs.exchange.take_list", request, LUMA_FS_EVENT_LIST, buf, size);
}
std::int32_t ex_cancel(const LumaMod* mod, LumaHandle request) {
    return cancel_request(mod, request, true, "fs.exchange.cancel");
}

// ---- The providers -------------------------------------------------------------------------------------------------

void mod_begin(void*, const LumaMod* mod) {
    if (state_of(mod) != nullptr) {
        return;  // the two providers share one record a mod
    }
    auto state = std::make_unique<ModState>();
    state->mod = mod;
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        if (const char* id = kernel->mod_id(mod)) {
            state->id = id;
        }
    }
    g_mods.push_back(std::move(state));
}

// After the mod's stop: its requests are the worker's to finish (answer frees them) or the kernel's to drop; nothing is
// posted or called for the mod from here on. Its writes still land, in order.
void mod_gone(void*, const LumaMod* mod) {
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    while (state->first != nullptr) {
        unlink(state->first);
    }
    state->open = 0;
}

void mod_end(void*, const LumaMod* mod) {
    for (auto it = g_mods.begin(); it != g_mods.end(); ++it) {
        if ((*it)->mod == mod) {
            g_mods.erase(it);
            return;
        }
    }
}

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaFsService kFs = {
    {sizeof(LumaFsService), LUMA_FS_MAJOR, LUMA_FS_MINOR},
    fs_read,
    fs_read_package,
    fs_take_read,
    fs_write,
    fs_remove,
    fs_move,
    fs_list,
    fs_list_package,
    fs_take_list,
    fs_cancel,
};

const LumaFsExchangeService kExchange = {
    {sizeof(LumaFsExchangeService), LUMA_FS_EXCHANGE_MAJOR, LUMA_FS_EXCHANGE_MINOR},
    ex_read,
    ex_take_read,
    ex_write,
    ex_remove,
    ex_move,
    ex_list,
    ex_take_list,
    ex_cancel,
};

// fs reports only the registry's rows (100-104): no rows of its own.
const LumaServiceProvider kFsProvider = {
    sizeof(LumaServiceProvider), LUMA_FS_NAME, &kFs.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider kExchangeProvider = {
    sizeof(LumaServiceProvider), LUMA_FS_EXCHANGE_NAME, &kExchange.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider* const kProviders[] = {&kFsProvider, &kExchangeProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept {
    g_log = log;
    return kProviders;
}

std::filesystem::path data_folder(std::string_view id) {
    const stdfs::path base = data_root();
    return base.empty() || !valid_id(id) ? stdfs::path() : base / std::string(id);
}

std::filesystem::path exchange_folder() {
    const stdfs::path base = galaxy::nebula_data_directory();
    return base.empty() ? stdfs::path() : base / "Exchange";
}

bool path_in_rule(std::string_view path, bool empty_ok) noexcept {
    try {
        std::vector<std::string> parts;
        return split_path(path, empty_ok, &parts);
    } catch (...) {
        return false;
    }
}

std::int32_t find_data_file(std::string_view id, std::string_view path, std::filesystem::path* out) {
    try {
        std::vector<std::string> parts;
        const stdfs::path folder = data_folder(id);
        if (folder.empty() || !split_path(path, false, &parts)) {
            return LUMA_E_NOT_FOUND;
        }
        const Place place = resolve(folder, parts, false);
        if (place.status != LUMA_OK) {
            return place.status;
        }
        if (place.kind != Kind::File) {
            return LUMA_E_NOT_FOUND;
        }
        *out = place.path;
        return LUMA_OK;
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::uint64_t data_bytes(std::string_view id) {
    try {
        const stdfs::path folder = data_folder(id);
        return !folder.empty() && kind_of(folder) == Kind::Folder ? folder_bytes(folder) : 0u;
    } catch (...) {
        return 0u;
    }
}

void forget_data(std::string_view id) {
    try {
        const stdfs::path folder = data_folder(id);
        if (folder.empty()) {
            return;
        }
        Request* request = new (std::nothrow) Request();
        if (request == nullptr) {
            return;
        }
        request->op = Op::Forget;
        request->folder = folder;
        Worker* w = worker();
        if (w == nullptr || !w->started) {
            delete request;
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(w->lock);
            w->jobs.push_back(request);
        }
        w->ready.notify_one();
    } catch (...) {
    }
}

}  // namespace galaxy::luma_fs
