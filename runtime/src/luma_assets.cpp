// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's assets service (docs/modding/services/assets.md). Layer 1: `assets`, the game's files by path through
// the host's AssetStore (game.pak, with the mods' store over it). The store is mapped for the whole session and never
// changes once the game runs, so a call is a lookup in its index and a mapped view: nothing is copied, nothing waits.
// Layer 2: files a mod provides, which the game is given before the store's (host_main svc_asset_*). Layer 3: a file
// as the disc has it cooked by the game's cooker on a thread of its own, then provided.
#include "galaxy/luma_assets_service.h"

#include "galaxy/asset_store.h"

#include <luma_assets.h>
#include <nebula_game_api.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

// The importer's run-time cook (assets.md section 4; nebula_import): one file as the disc has it, cooked as
// the store holds it. luma_cook_entry never returns null but for want of memory; luma_cooked_error is null when it
// cooked (else the cooker's words, English UTF-8, valid until free); its items in the store's order, each a path inside
// ("" the file itself) and bytes, or a folder (*folder 1, bytes null); everything valid until luma_cooked_free. Safe to
// call from any one thread while the importer's other calls run on others.
extern "C" {
typedef struct LumaCooked LumaCooked;
}
using LumaCookEntry = LumaCooked* (*)(const char* name, const std::uint8_t* bytes, std::uint64_t size);
using LumaCookedError = const char* (*)(const LumaCooked* cooked);
using LumaCookedCount = std::uint32_t (*)(const LumaCooked* cooked);
using LumaCookedItem = const std::uint8_t* (*)(const LumaCooked* cooked, std::uint32_t index, const char** path,
                                                std::uint64_t* size, std::uint32_t* folder);
using LumaCookedFree = void (*)(LumaCooked* cooked);
#if defined(__APPLE__)
// The importer is linked into the program on Apple (platform/apple/metal.cmake); an importer without the cook leaves
// these null.
extern "C" {
__attribute__((weak_import)) LumaCooked* luma_cook_entry(const char* name, const std::uint8_t* bytes, std::uint64_t size);
__attribute__((weak_import)) const char* luma_cooked_error(const LumaCooked* cooked);
__attribute__((weak_import)) std::uint32_t luma_cooked_count(const LumaCooked* cooked);
__attribute__((weak_import)) const std::uint8_t* luma_cooked_item(const LumaCooked* cooked, std::uint32_t index,
                                                                  const char** path, std::uint64_t* size,
                                                                  std::uint32_t* folder);
__attribute__((weak_import)) void luma_cooked_free(LumaCooked* cooked);
}
#endif

namespace galaxy::luma_assets {
namespace {

static_assert(sizeof(LumaAssetInfo) == 32u, "LumaAssetInfo 1.0 is 32 bytes");
static_assert(int{LUMA_ASSET_MODEL} == int{NEBULA_ASSET_KIND_MODEL} &&
                  int{LUMA_ASSET_TEXTURE} == int{NEBULA_ASSET_KIND_TEXTURE} &&
                  int{LUMA_ASSET_LAYOUT_ANIMATION} == int{NEBULA_ASSET_KIND_LAYOUT_ANIMATION} &&
                  int{LUMA_ASSET_LAYOUT_ANIMATION} + 1 == int{NEBULA_ASSET_KIND_COUNT},
              "LumaAssetKind is the importer's kind, value for value");

std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
std::atomic<const assets::AssetStore*> g_store{nullptr};
// A 0-byte file's bytes: a pointer that is not null (header).
constexpr unsigned char kNoBytes[1] = {0};

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr) noexcept {
    const char* args[1] = {a};
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, a == nullptr ? 0u : 1u);
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

// The checks every call makes: the mod, the thread, the path and the flags. LUMA_OK and `path` in the store's form: a
// mod path `<id>:<path>` (luma.md 5) is the mod's store folder Luma/<id>/<path> (mods.cpp plan_mod_changes).
std::int32_t begin(const LumaMod* mod, const char* where, const char* text, std::uint32_t flags,
                   std::string& path) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (text == nullptr) {
        return argument(mod, where, "path");
    }
    const std::size_t length = ::strnlen(text, LUMA_ASSETS_PATH_MOST + 1u);
    if (length > LUMA_ASSETS_PATH_MOST) {
        return argument(mod, where, "path");
    }
    if ((flags & ~LUMA_ASSETS_GAME_ONLY) != 0u) {
        return argument(mod, where, "flags");
    }
    try {
        const std::string_view given(text, length);
        const std::size_t colon = given.find(':');
        if (colon != std::string_view::npos && colon != 0u &&
            given.substr(0, colon).find('/') == std::string_view::npos) {
            path = "Luma/";
            path.append(given.substr(0, colon)).append("/").append(given.substr(colon + 1u));
        } else {
            path = assets::store_path(given);
        }
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

// ---- Layers 2 and 3: the files mods provide. ----
//
// A provide is built (its nodes and one block of its files side by side, in 32-byte steps as the cooker places them)
// off the table's lock, then committed under it. The table only grows: an entry once given keeps its index, its name and
// its bytes until the process ends, because the game may hold what it was told (an index, a name pointer, a view) on
// any thread; a withdraw only takes the path's key away. One mod's provides are capped (LUMA_ASSETS_PROVIDE_MOST over
// its run), so what stays is bounded.

constexpr std::uint64_t kStep = 32u;  // the cooker's file step (cook/mod.rs ALIGN)
constexpr std::align_val_t kBlockAlign{64};

struct BlockFree {
    void operator()(std::uint8_t* p) const noexcept { ::operator delete[](p, kBlockAlign); }
};
using Block = std::unique_ptr<std::uint8_t[], BlockFree>;

struct Node {
    std::string name;  // as the mod gave it (a top's: its path's last part)
    std::int32_t parent = -1;
    bool folder = false;
    bool archive = false;
    std::uint8_t kind = 0;
    std::uint64_t position = 0;  // in its top's block; a folder's: its first file's
    std::uint64_t length = 0;    // a file's bytes; a folder's, from its first file to its last one's end
    std::vector<std::int32_t> children;
    const std::uint8_t* block = nullptr;  // its top's block
};

struct Top {
    const LumaMod* mod = nullptr;
    std::int32_t root = -1;
};

// Never destroyed: the game's loading threads may ask until the process ends.
struct Table {
    std::shared_mutex lock;
    std::deque<Node> nodes;  // index kProvidedFirst + i
    std::deque<Block> blocks;
    std::unordered_map<std::string, Top> tops;  // by key_of_normal
    std::unordered_map<const LumaMod*, std::uint64_t> spent;
};
Table& table() {
    static Table* const t = new Table;
    return *t;
}
std::atomic<bool> g_any{false};

char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// A path in the store's form with "", "." and ".." parts settled, letters as given ("" for the root or above it).
std::string normal_of(std::string_view path) {
    const std::string store = assets::store_path(path);
    std::vector<std::string_view> parts;
    std::string_view rest = store;
    while (!rest.empty()) {
        const std::size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1u);
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
            continue;
        }
        parts.push_back(part);
    }
    std::string out;
    for (const std::string_view part : parts) {
        if (!out.empty()) {
            out += '/';
        }
        out.append(part);
    }
    return out;
}

std::string key_of_normal(std::string_view normal) {
    std::string key(normal);
    for (char& c : key) {
        c = lower(c);
    }
    return key;
}

// The importer's kind of a file by its name (cook/store.rs resource_kind).
std::uint8_t kind_of(std::string_view name) noexcept {
    const std::size_t slash = name.rfind('/');
    const std::string_view file = slash == std::string_view::npos ? name : name.substr(slash + 1u);
    const std::size_t dot = file.rfind('.');
    if (dot == std::string_view::npos) {
        return 0u;
    }
    char extension[8] = {};
    const std::string_view given = file.substr(dot + 1u);
    if (given.size() >= sizeof(extension)) {
        return 0u;
    }
    for (std::size_t i = 0; i < given.size(); ++i) {
        extension[i] = lower(given[i]);
    }
    static constexpr struct {
        const char* extension;
        std::uint8_t kind;
    } kKinds[] = {{"bdl", 1},  {"bmd", 1},  {"bck", 2},  {"bca", 2},    {"btk", 3},  {"bpk", 4},
                  {"btp", 5},  {"blk", 6},  {"brk", 7},  {"bas", 8},    {"bmt", 9},  {"bva", 10},
                  {"banmt", 11}, {"bti", 12}, {"brlyt", 13}, {"brlan", 14}};
    for (const auto& known : kKinds) {
        if (std::strcmp(extension, known.extension) == 0) {
            return known.kind;
        }
    }
    return 0u;
}

assets::Entry entry_of(const Node& node, std::int32_t index) {
    assets::Entry entry;
    entry.index = index;
    entry.folder = node.folder;
    entry.archive = node.archive;
    entry.kind = node.folder ? 0u : node.kind;
    entry.length = node.length;
    entry.position = node.position;
    entry.children = static_cast<std::uint32_t>(node.children.size());
    entry.from_overlay = true;
    entry.name = node.name.c_str();
    return entry;
}

// One file or folder to provide: a mod's LumaAssetFile, or a cooker's item.
struct Piece {
    std::string_view path;  // inside the folder; "" the top itself
    const std::uint8_t* data = nullptr;
    std::uint64_t bytes = 0;
    bool folder = false;  // a cooker's empty folder
};

// A provide built off the lock: nodes with indices local to it (0 the top), and its block.
struct Built {
    std::vector<Node> nodes;
    Block block;
    std::uint64_t size = 0;  // the block's bytes, as counted against the mod
};

// Whether a part of a file's path inside the folder is one the table takes.
bool part_ok(std::string_view part) noexcept {
    return !part.empty() && part != "." && part != ".." && part.find('\\') == std::string_view::npos &&
           part.find(':') == std::string_view::npos;
}

// LUMA_OK and `built`; LUMA_E_ARGUMENT with the argument's name in *bad; LUMA_E_LIMIT, the files past the most.
std::int32_t build(std::string_view top_name, const std::vector<Piece>& pieces, Built& built, const char** bad) {
    built.nodes.clear();
    const bool single = pieces.size() == 1u && pieces[0].path.empty() && !pieces[0].folder;
    Node top;
    top.name = std::string(top_name);
    if (single) {
        top.kind = kind_of(top_name);
        top.length = pieces[0].bytes;
    } else {
        top.folder = true;
        top.archive = true;
    }
    built.nodes.push_back(std::move(top));
    // Where each file goes in the block (one file: at 0).
    std::vector<std::uint64_t> at(pieces.size(), 0u);
    std::uint64_t end = 0u;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (pieces[i].folder) {
            continue;
        }
        if (pieces[i].bytes > LUMA_ASSETS_PROVIDE_MOST || end > LUMA_ASSETS_PROVIDE_MOST) {
            return LUMA_E_LIMIT;
        }
        at[i] = (end + kStep - 1u) / kStep * kStep;
        end = at[i] + pieces[i].bytes;
    }
    if (end > LUMA_ASSETS_PROVIDE_MOST) {
        return LUMA_E_LIMIT;
    }
    built.size = std::max<std::uint64_t>(end, 1u);
    built.block.reset(static_cast<std::uint8_t*>(::operator new[](static_cast<std::size_t>(built.size), kBlockAlign)));
    std::memset(built.block.get(), 0, static_cast<std::size_t>(built.size));
    if (single) {
        if (pieces[0].bytes != 0u) {
            std::memcpy(built.block.get(), pieces[0].data, static_cast<std::size_t>(pieces[0].bytes));
        }
        return LUMA_OK;
    }
    // Folders by their lower-case path inside ("" the top), files by theirs.
    std::unordered_map<std::string, std::int32_t> folders{{std::string(), 0}};
    std::unordered_map<std::string, std::int32_t> files;
    const auto folder_for = [&](std::string_view path, std::int32_t& out) -> bool {
        std::int32_t parent = 0;
        std::string key;
        std::string_view rest = path;
        while (!rest.empty()) {
            const std::size_t slash = rest.find('/');
            const std::string_view part = rest.substr(0, slash);
            rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1u);
            if (!part_ok(part)) {
                return false;
            }
            if (!key.empty()) {
                key += '/';
            }
            for (const char c : part) {
                key += lower(c);
            }
            if (files.count(key) != 0u) {
                return false;  // a file where this folder is
            }
            const auto found = folders.find(key);
            if (found != folders.end()) {
                parent = found->second;
                continue;
            }
            Node folder;
            folder.name = std::string(part);
            folder.parent = parent;
            folder.folder = true;
            const std::int32_t index = static_cast<std::int32_t>(built.nodes.size());
            built.nodes[static_cast<std::size_t>(parent)].children.push_back(index);
            built.nodes.push_back(std::move(folder));
            folders.emplace(key, index);
            parent = index;
        }
        out = parent;
        return true;
    };
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        if (piece.folder && piece.path.empty()) {
            continue;  // the cooker's empty archive: the top itself
        }
        if (piece.path.empty() || piece.path.front() == '/' || piece.path.back() == '/') {
            *bad = "files";
            return LUMA_E_ARGUMENT;
        }
        if (piece.folder) {
            std::int32_t ignored = 0;
            if (!folder_for(piece.path, ignored)) {
                *bad = "files";
                return LUMA_E_ARGUMENT;
            }
            continue;
        }
        const std::size_t slash = piece.path.rfind('/');
        const std::string_view name = slash == std::string_view::npos ? piece.path : piece.path.substr(slash + 1u);
        std::int32_t parent = 0;
        if (!part_ok(name) || (slash != std::string_view::npos && !folder_for(piece.path.substr(0, slash), parent))) {
            *bad = "files";
            return LUMA_E_ARGUMENT;
        }
        std::string key;
        for (const char c : piece.path) {
            key += lower(c);
        }
        if (files.count(key) != 0u || folders.count(key) != 0u) {
            *bad = "files";  // given twice, or a file where a folder is
            return LUMA_E_ARGUMENT;
        }
        Node file;
        file.name = std::string(name);
        file.parent = parent;
        file.kind = kind_of(name);
        file.position = at[i];
        file.length = piece.bytes;
        if (piece.bytes != 0u) {
            std::memcpy(built.block.get() + at[i], piece.data, static_cast<std::size_t>(piece.bytes));
        }
        const std::int32_t index = static_cast<std::int32_t>(built.nodes.size());
        built.nodes[static_cast<std::size_t>(parent)].children.push_back(index);
        built.nodes.push_back(std::move(file));
        files.emplace(std::move(key), index);
    }
    // Each folder's span, from its first file to its last one's end: a node comes after its parent in nodes, so a walk
    // from the end widens each parent after all of its own children.
    std::vector<std::uint64_t> first(built.nodes.size(), UINT64_MAX);
    std::vector<std::uint64_t> last(built.nodes.size(), 0u);
    for (std::size_t i = built.nodes.size(); i-- > 0u;) {
        Node& node = built.nodes[i];
        if (!node.folder) {
            first[i] = node.position;
            last[i] = node.position + node.length;
        } else if (first[i] != UINT64_MAX) {
            node.position = first[i];
            node.length = last[i] - first[i];
        }
        if (node.parent >= 0 && first[i] != UINT64_MAX) {
            const std::size_t parent = static_cast<std::size_t>(node.parent);
            first[parent] = std::min(first[parent], first[i]);
            last[parent] = std::max(last[parent], last[i]);
        }
    }
    return LUMA_OK;
}

// Under the table's lock: LUMA_OK and the provide in the table (the mod's own earlier provide at the key replaced), or
// an answer: LUMA_E_EXISTS, LUMA_E_FORMAT, LUMA_E_LIMIT.
std::int32_t commit(Table& t, const LumaMod* mod, const std::string& key, Built& built) {
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    // Inside an archive (the game's, a mods' store's, a provided one) or under a file: the game reads the archive whole
    // and never looks this path up.
    for (std::size_t slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash + 1u)) {
        const std::string prefix = key.substr(0, slash);
        const auto top = t.tops.find(prefix);
        if (top != t.tops.end()) {
            const Node& node = t.nodes[static_cast<std::size_t>(top->second.root - kProvidedFirst)];
            if (!node.folder || node.archive) {
                return LUMA_E_FORMAT;
            }
        }
        if (store != nullptr) {
            const std::optional<assets::Entry> found = store->find(prefix);
            if (found && (!found->folder || found->archive)) {
                return LUMA_E_FORMAT;
            }
        }
    }
    // What the game has at the path decides its form first: a plain folder, or a file where this is an archive (or
    // the other way), is never provided, whoever provides what under it (2804).
    const bool folder = built.nodes[0].folder;
    if (store != nullptr) {
        if (const std::optional<assets::Entry> game = store->find(key)) {
            if ((game->folder && !game->archive) || game->folder != folder) {
                return LUMA_E_FORMAT;
            }
        }
    }
    // A path under this one provided already would be hidden by it.
    const std::string under = key + '/';
    for (const auto& [other, top] : t.tops) {
        if (other.size() > under.size() && other.compare(0, under.size(), under) == 0) {
            return LUMA_E_EXISTS;
        }
    }
    const auto same = t.tops.find(key);
    if (same != t.tops.end() && same->second.mod != mod) {
        return LUMA_E_EXISTS;
    }
    std::uint64_t& spent = t.spent[mod];
    if (spent + built.size > LUMA_ASSETS_PROVIDE_MOST ||
        t.nodes.size() + built.nodes.size() > static_cast<std::size_t>(INT32_MAX - kProvidedFirst)) {
        return LUMA_E_LIMIT;
    }
    const std::int32_t base = kProvidedFirst + static_cast<std::int32_t>(t.nodes.size());
    const std::uint8_t* const block = built.block.get();
    t.blocks.push_back(std::move(built.block));
    for (Node& node : built.nodes) {
        node.parent = node.parent >= 0 ? base + node.parent : -1;
        for (std::int32_t& child : node.children) {
            child += base;
        }
        node.block = block;
        t.nodes.push_back(std::move(node));
    }
    spent += built.size;
    t.tops[key] = Top{mod, base};
    g_any.store(true, std::memory_order_release);
    return LUMA_OK;
}

// Builds and commits `normal` (normal_of's form): LUMA_OK (*held the mod's provided bytes now), an answer (EXISTS,
// FORMAT), or LIMIT and ARGUMENT (*bad) for the caller to report.
std::int32_t provide_pieces(const LumaMod* mod, const std::string& normal, const std::vector<Piece>& pieces,
                            std::uint64_t* held, const char** bad) {
    const std::size_t slash = normal.rfind('/');
    const std::string_view name = slash == std::string::npos ? std::string_view(normal) : std::string_view(normal).substr(slash + 1u);
    Built built;
    if (const std::int32_t status = build(name, pieces, built, bad); status != LUMA_OK) {
        return status;
    }
    const std::string key = key_of_normal(normal);
    Table& t = table();
    const std::unique_lock lock(t.lock);
    const std::int32_t status = commit(t, mod, key, built);
    if (status == LUMA_OK && held != nullptr) {
        *held = t.spent[mod];
    }
    return status;
}

void withdraw_all(const LumaMod* mod) {
    Table& t = table();
    const std::unique_lock lock(t.lock);
    for (auto it = t.tops.begin(); it != t.tops.end();) {
        it = it->second.mod == mod ? t.tops.erase(it) : std::next(it);
    }
}

std::optional<assets::Entry> provided_at(std::string_view path) {
    if (!g_any.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    const std::string key = key_of_normal(normal_of(path));
    Table& t = table();
    const std::shared_lock lock(t.lock);
    const auto top = t.tops.find(key);
    if (top == t.tops.end()) {
        return std::nullopt;
    }
    return entry_of(t.nodes[static_cast<std::size_t>(top->second.root - kProvidedFirst)], top->second.root);
}

// Under the table's lock (shared): the node of a provided index, or null.
const Node* node_at(const Table& t, std::int32_t index) noexcept {
    if (!is_provided(index)) {
        return nullptr;
    }
    const std::size_t at = static_cast<std::size_t>(index - kProvidedFirst);
    return at < t.nodes.size() ? &t.nodes[at] : nullptr;
}

// What is at a path: with GAME_ONLY the game's own; else a mod's provided file first, then the store's (merged).
std::optional<assets::Entry> look(const assets::AssetStore& store, std::string_view path, std::uint32_t flags) {
    if ((flags & LUMA_ASSETS_GAME_ONLY) != 0u) {
        return store.find_game(path);
    }
    if (std::optional<assets::Entry> provided = provided_at(path)) {
        return provided;
    }
    return store.find(path);
}

// How many entries the mods add to a game folder (added_child's indices).
std::uint32_t added_count(const assets::AssetStore& store, std::string_view path) {
    std::uint32_t count = 0u;
    while (store.added_child(path, count)) {
        ++count;
    }
    return count;
}

// A folder's entries as `entry` gives them: a mod's folder its own; the game's its own and then the mods' additions
// (none with GAME_ONLY).
std::uint32_t entries_of(const assets::AssetStore& store, const assets::Entry& folder, std::string_view path,
                         std::uint32_t flags) {
    if (!folder.folder) {
        return 0u;
    }
    if (folder.from_overlay || (flags & LUMA_ASSETS_GAME_ONLY) != 0u) {
        return folder.children;
    }
    return folder.children + added_count(store, path);
}

// *out from an entry (only the bytes the mod's struct has).
void fill(const assets::AssetStore& store, const assets::Entry& found, std::string_view path, std::uint32_t flags,
          LumaAssetInfo* out) {
    LumaAssetInfo info{};
    info.size = out->size;
    info.folder = found.folder ? 1u : 0u;
    info.archive = found.archive ? 1u : 0u;
    info.kind = found.folder ? 0u : found.kind;
    info.from_mod = found.from_overlay ? 1u : 0u;
    info.entries = entries_of(store, found, path, flags);
    info.bytes = found.folder ? 0u : found.length;
    std::memcpy(out, &info, std::min<std::size_t>(out->size, sizeof(info)));
}

std::int32_t assets_find(const LumaMod* mod, const char* text, std::uint32_t flags, LumaAssetInfo* out) noexcept {
    static constexpr const char* where = "assets.find";
    std::string path;
    if (const std::int32_t status = begin(mod, where, text, flags, path); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr || out->size < 8u) {
        return argument(mod, where, "out");
    }
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    try {
        const std::optional<assets::Entry> found = store != nullptr ? look(*store, path, flags) : std::nullopt;
        if (!found) {
            return LUMA_E_NOT_FOUND;
        }
        fill(*store, *found, path, flags, out);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int32_t assets_bytes(const LumaMod* mod, const char* text, std::uint32_t flags, const void** data,
                          std::uint64_t* bytes) noexcept {
    static constexpr const char* where = "assets.bytes";
    std::string path;
    if (const std::int32_t status = begin(mod, where, text, flags, path); status != LUMA_OK) {
        return status;
    }
    if (data == nullptr) {
        return argument(mod, where, "data");
    }
    if (bytes == nullptr) {
        return argument(mod, where, "bytes");
    }
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    try {
        const std::optional<assets::Entry> found = store != nullptr ? look(*store, path, flags) : std::nullopt;
        if (!found) {
            return LUMA_E_NOT_FOUND;
        }
        if (found->folder) {
            return LUMA_E_FORMAT;
        }
        if (found->length == 0u) {
            *data = kNoBytes;
            *bytes = 0u;
            return LUMA_OK;
        }
        const std::uint8_t* const mapped = is_provided(found->index)
                                               ? provided_view(found->index)
                                               : store->map(found->position, static_cast<std::size_t>(found->length));
        if (mapped == nullptr) {
            return LUMA_E_IO;
        }
        *data = mapped;
        *bytes = found->length;
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int32_t assets_entry(const LumaMod* mod, const char* text, std::uint32_t flags, std::uint32_t index, char* name,
                          std::uint32_t name_size, std::uint32_t* needed, LumaAssetInfo* out) noexcept {
    static constexpr const char* where = "assets.entry";
    std::string path;
    if (const std::int32_t status = begin(mod, where, text, flags, path); status != LUMA_OK) {
        return status;
    }
    if (name == nullptr && name_size != 0u) {
        return argument(mod, where, "name");
    }
    if (out != nullptr && out->size < 8u) {
        return argument(mod, where, "out");
    }
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    try {
        const std::optional<assets::Entry> folder = store != nullptr ? look(*store, path, flags) : std::nullopt;
        if (!folder) {
            return LUMA_E_NOT_FOUND;
        }
        if (!folder->folder) {
            return LUMA_E_FORMAT;
        }
        std::optional<assets::Entry> found;
        if (index < folder->children) {
            found = is_provided(folder->index) ? provided_child(folder->index, index) : store->child(folder->index, index);
        } else if (!folder->from_overlay && (flags & LUMA_ASSETS_GAME_ONLY) == 0u) {
            found = store->added_child(path, index - folder->children);
        }
        if (!found || found->name == nullptr) {
            return LUMA_E_NOT_FOUND;
        }
        const std::string_view entry_name(found->name);
        // The game's entry of a game folder may be a mod's file now: what the game reads at that path.
        std::string full = path;
        if (!full.empty() && full.back() != '/') {
            full += '/';
        }
        full += entry_name;
        if (!found->from_overlay && (flags & LUMA_ASSETS_GAME_ONLY) == 0u) {
            if (std::optional<assets::Entry> merged = look(*store, full, flags)) {
                found = merged;
            }
        }
        if (out != nullptr) {
            fill(*store, *found, full, flags, out);
        }
        const std::uint32_t size = static_cast<std::uint32_t>(entry_name.size()) + 1u;
        if (name_size < size) {
            if (needed != nullptr) {
                *needed = size;
            }
            if (name_size != 0u) {
                name[0] = '\0';
            }
            return LUMA_E_TOO_LARGE;
        }
        std::memcpy(name, entry_name.data(), entry_name.size());
        name[entry_name.size()] = '\0';
        if (needed != nullptr) {
            *needed = size;
        }
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

// ---- Layer 3: cook. ----
//
// The game's cooker (nebula_import, the same one the start's cook runs, part of the game) turns a file
// as the disc has it into the game's form: luma_cook_entry, the importer's cook_entry behind a C call (contract
// services/assets.md 6). One thread cooks, one file at a time, in the order asked; the answer is posted to the mod and
// provided on the game thread when it is delivered, so the table's rules apply to the game as it is then.

struct Cooker {
    LumaCookEntry entry = nullptr;
    LumaCookedError error = nullptr;
    LumaCookedCount count = nullptr;
    LumaCookedItem item = nullptr;
    LumaCookedFree free = nullptr;
};

// The importer's cook calls, or null when this game's importer has none (loaded once, kept loaded).
const Cooker* cooker() {
    static const Cooker* const found = []() -> const Cooker* {
        Cooker c;
#if defined(__APPLE__)
        c.entry = &luma_cook_entry;
        c.error = &luma_cooked_error;
        c.count = &luma_cooked_count;
        c.item = &luma_cooked_item;
        c.free = &luma_cooked_free;
#else
#if defined(_WIN32)
        // As mod_start_check.cpp loads it: its own C runtime comes from System32.
        HMODULE const library =
            LoadLibraryExW(L"nebula_import.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto symbol = [library](const char* name) -> void* {
            return library != nullptr ? reinterpret_cast<void*>(GetProcAddress(library, name)) : nullptr;
        };
#else
        void* const library = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL);
        const auto symbol = [library](const char* name) -> void* {
            return library != nullptr ? dlsym(library, name) : nullptr;
        };
#endif
        c.entry = reinterpret_cast<LumaCookEntry>(symbol("luma_cook_entry"));
        c.error = reinterpret_cast<LumaCookedError>(symbol("luma_cooked_error"));
        c.count = reinterpret_cast<LumaCookedCount>(symbol("luma_cooked_count"));
        c.item = reinterpret_cast<LumaCookedItem>(symbol("luma_cooked_item"));
        c.free = reinterpret_cast<LumaCookedFree>(symbol("luma_cooked_free"));
#endif
        if (c.entry == nullptr || c.error == nullptr || c.count == nullptr || c.item == nullptr || c.free == nullptr) {
            return nullptr;
        }
        return new Cooker(c);
    }();
    return found;
}

struct CookJob;
// The kernel's node and its job: standard layout, so deliver and drop get the link from the node, and the job from it.
struct CookLink {
    LumaEventNode node;
    CookJob* job;
};

struct CookJob {
    CookLink link{};
    const LumaMod* mod = nullptr;
    std::string normal;  // where it goes (normal_of)
    std::string name;    // the path as the mod gave it (".arc" and all): the cooker's name for the file
    std::vector<std::uint8_t> source;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    LumaHandle request = 0;
    enum class State { Waiting, Cooking, Posted } state = State::Waiting;
    bool cancelled = false;  // the mod is gone while it cooks: the cooking thread forgets it
    // The cooker's answer.
    std::int32_t status = LUMA_OK;
    std::string words;
    struct Item {
        std::string path;
        std::vector<std::uint8_t> bytes;
        bool folder = false;
    };
    std::vector<Item> items;
};

// Never destroyed: the cooking thread waits on it until the process ends.
struct Cooks {
    std::mutex lock;
    std::condition_variable wake;
    std::vector<std::unique_ptr<CookJob>> jobs;  // waiting, cooking, or posted and not yet delivered or dropped
    bool thread = false;
};
Cooks& cooks() {
    static Cooks* const c = new Cooks;
    return *c;
}
std::atomic<std::uint64_t> g_requests{0};

void forget_locked(Cooks& c, const CookJob* job) {
    for (auto it = c.jobs.begin(); it != c.jobs.end(); ++it) {
        if (it->get() == job) {
            c.jobs.erase(it);
            return;
        }
    }
}

void cook_one(const Cooker& cooker, CookJob& job) {
    LumaCooked* const cooked = cooker.entry(job.name.c_str(), job.source.data(), job.source.size());
    if (cooked == nullptr) {
        job.status = LUMA_E_MEMORY;
        return;
    }
    if (const char* const words = cooker.error(cooked)) {
        job.status = LUMA_E_FORMAT;
        job.words = words;
    } else {
        const std::uint32_t count = cooker.count(cooked);
        job.items.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const char* path = nullptr;
            std::uint64_t size = 0;
            std::uint32_t folder = 0;
            const std::uint8_t* const bytes = cooker.item(cooked, i, &path, &size, &folder);
            CookJob::Item item;
            item.path = path != nullptr ? path : "";
            item.folder = folder != 0u;
            if (!item.folder && bytes != nullptr) {
                item.bytes.assign(bytes, bytes + size);
            }
            job.items.push_back(std::move(item));
        }
    }
    cooker.free(cooked);
}

void cook_loop() {
    Cooks& c = cooks();
    const Cooker& found = *cooker();
    for (;;) {
        CookJob* job = nullptr;
        {
            std::unique_lock lock(c.lock);
            c.wake.wait(lock, [&c, &job] {
                for (const std::unique_ptr<CookJob>& waiting : c.jobs) {
                    if (waiting->state == CookJob::State::Waiting) {
                        job = waiting.get();
                        return true;
                    }
                }
                return false;
            });
            job->state = CookJob::State::Cooking;
        }
        try {
            cook_one(found, *job);
        } catch (...) {
            job->status = LUMA_E_MEMORY;
            job->items.clear();
        }
        job->source = {};
        const std::lock_guard lock(c.lock);
        const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
        // Posting under the lock: mod_gone takes it too, so after it returns nothing posts for the mod.
        if (job->cancelled || kernel == nullptr || kernel->post(&job->link.node) != LUMA_OK) {
            forget_locked(c, job);
            continue;
        }
        job->state = CookJob::State::Posted;
    }
}

// Game thread: the cook's answer, provided first when it cooked.
void cook_deliver(LumaEventNode* node) {
    CookJob* const job = reinterpret_cast<CookLink*>(node)->job;
    std::int32_t status = job->status;
    std::uint64_t held = 0;
    if (status == LUMA_OK) {
        std::vector<Piece> pieces;
        pieces.reserve(job->items.size());
        for (const CookJob::Item& item : job->items) {
            pieces.push_back(Piece{item.path, item.bytes.data(), item.bytes.size(), item.folder});
        }
        const char* bad = nullptr;
        try {
            status = provide_pieces(job->mod, job->normal, pieces, &held, &bad);
        } catch (...) {
            status = LUMA_E_MEMORY;
        }
        if (status == LUMA_E_LIMIT) {
            report(job->mod, "assets.cook", 104u, "268435456");  // Service.Limit, as provide reports it
        }
        if (status == LUMA_E_ARGUMENT) {
            status = LUMA_E_FORMAT;  // the cooker gave paths the table does not take: the file's fault, not the mod's
            job->words = "the cooker gave a file path the game cannot hold";
        }
    }
    LumaEvent event{};
    event.size = sizeof(LumaEvent);
    event.kind = LUMA_ASSETS_EVENT_COOKED;
    event.status = status;
    event.handle = job->request;
    if (status == LUMA_E_FORMAT && !job->words.empty()) {
        event.data = job->words.data();
        event.data_size = job->words.size();
    }
    event.value = held;
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->call(job->mod, LUMA_ASSETS_NAME, job->fn, job->user, &event);
    }
    Cooks& c = cooks();
    const std::lock_guard lock(c.lock);
    forget_locked(c, job);
}

void cook_drop(LumaEventNode* node) {
    Cooks& c = cooks();
    const std::lock_guard lock(c.lock);
    forget_locked(c, reinterpret_cast<CookLink*>(node)->job);
}

void cancel_cooks(const LumaMod* mod) {
    Cooks& c = cooks();
    const std::lock_guard lock(c.lock);
    for (auto it = c.jobs.begin(); it != c.jobs.end();) {
        CookJob& job = **it;
        if (job.mod != mod) {
            ++it;
        } else if (job.state == CookJob::State::Waiting) {
            it = c.jobs.erase(it);
        } else {
            // Cooking: the thread forgets it when done. Posted: the kernel drops the node (cook_drop).
            job.cancelled = true;
            ++it;
        }
    }
}

// ---- The service's calls for layers 2 and 3. ----

std::int32_t assets_provide(const LumaMod* mod, const char* text, const LumaAssetFile* files,
                            std::uint32_t count) noexcept {
    static constexpr const char* where = "assets.provide";
    std::string path;
    if (const std::int32_t status = begin(mod, where, text, 0u, path); status != LUMA_OK) {
        return status;
    }
    if (files == nullptr) {
        return argument(mod, where, "files");
    }
    if (count == 0u || count > LUMA_ASSETS_PROVIDE_FILES_MOST) {
        return argument(mod, where, "count");
    }
    try {
        const std::string normal = normal_of(path);
        if (normal.empty()) {
            return argument(mod, where, "path");
        }
        std::vector<Piece> pieces;
        pieces.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const LumaAssetFile& file = files[i];
            if (file.data == nullptr && file.bytes != 0u) {
                return argument(mod, where, "files");
            }
            const std::string_view inside =
                file.path != nullptr ? std::string_view(file.path, ::strnlen(file.path, LUMA_ASSETS_PATH_MOST + 1u))
                                     : std::string_view{};
            if (inside.size() > LUMA_ASSETS_PATH_MOST || (inside.empty() && count != 1u)) {
                return argument(mod, where, "files");
            }
            pieces.push_back(Piece{inside, static_cast<const std::uint8_t*>(file.data), file.bytes, false});
        }
        const char* bad = "files";
        const std::int32_t status = provide_pieces(mod, normal, pieces, nullptr, &bad);
        if (status == LUMA_E_ARGUMENT) {
            return argument(mod, where, bad);
        }
        if (status == LUMA_E_LIMIT) {
            report(mod, where, 104u, "268435456");  // Service.Limit: LUMA_ASSETS_PROVIDE_MOST bytes
        }
        return status;
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t assets_withdraw(const LumaMod* mod, const char* text) noexcept {
    static constexpr const char* where = "assets.withdraw";
    std::string path;
    if (const std::int32_t status = begin(mod, where, text, 0u, path); status != LUMA_OK) {
        return status;
    }
    try {
        const std::string key = key_of_normal(normal_of(path));
        Table& t = table();
        const std::unique_lock lock(t.lock);
        const auto top = t.tops.find(key);
        if (top == t.tops.end() || top->second.mod != mod) {
            return LUMA_E_NOT_FOUND;
        }
        t.tops.erase(top);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int32_t assets_cook(const LumaMod* mod, const char* text, const void* data, std::uint64_t bytes, LumaEventFn fn,
                         void* user, LumaHandle* request) noexcept {
    static constexpr const char* where = "assets.cook";
    std::string path;
    if (const std::int32_t status = begin(mod, where, text, 0u, path); status != LUMA_OK) {
        return status;
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (request == nullptr) {
        return argument(mod, where, "request");
    }
    if (data == nullptr || bytes == 0u) {
        return argument(mod, where, "data");
    }
    if (bytes > LUMA_ASSETS_PROVIDE_MOST) {
        report(mod, where, 104u, "268435456");  // Service.Limit
        return LUMA_E_TOO_LARGE;
    }
    try {
        std::string normal = normal_of(path);
        if (normal.empty()) {
            return argument(mod, where, "path");
        }
        if (cooker() == nullptr) {
            return LUMA_E_ABSENT;
        }
        Cooks& c = cooks();
        const std::lock_guard lock(c.lock);
        std::uint32_t waiting = 0u;
        for (const std::unique_ptr<CookJob>& job : c.jobs) {
            waiting += job->mod == mod && !job->cancelled ? 1u : 0u;
        }
        if (waiting >= LUMA_ASSETS_COOKS_MOST) {
            return LUMA_E_BUSY;
        }
        auto job = std::make_unique<CookJob>();
        job->link.job = job.get();
        job->link.node.mod = mod;
        job->link.node.deliver = cook_deliver;
        job->link.node.drop = cook_drop;
        job->mod = mod;
        job->normal = std::move(normal);
        job->name = path;
        job->source.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + bytes);
        job->fn = fn;
        job->user = user;
        job->request = g_requests.fetch_add(1u, std::memory_order_relaxed) + 1u;
        *request = job->request;
        c.jobs.push_back(std::move(job));
        if (!c.thread) {
            std::thread(cook_loop).detach();
            c.thread = true;
        }
        c.wake.notify_one();
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_PENDING;
}

void assets_mod_gone(void*, const LumaMod* mod) {
    withdraw_all(mod);
    cancel_cooks(mod);
}

void assets_mod_end(void*, const LumaMod* mod) {
    Table& t = table();
    const std::unique_lock lock(t.lock);
    t.spent.erase(mod);  // its bytes stay (the game may hold them); a later mod at this address starts at 0
}

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaAssetsService kAssets = {
    {sizeof(LumaAssetsService), LUMA_ASSETS_MAJOR, LUMA_ASSETS_MINOR},
    assets_find,
    assets_bytes,
    assets_entry,
    assets_provide,
    assets_withdraw,
    assets_cook,
};

// assets reports only the registry's rows (100, 101, 104). A mod's stop withdraws what it provides and cancels its
// cooks; its end forgets its count.
const LumaServiceProvider kAssetsProvider = {
    sizeof(LumaServiceProvider), LUMA_ASSETS_NAME, &kAssets.base, nullptr, nullptr, 0u, nullptr,
    nullptr, assets_mod_gone, assets_mod_end, registered,
};
const LumaServiceProvider* const kProviders[] = {&kAssetsProvider};

}  // namespace

void set_store(const galaxy::assets::AssetStore* store) noexcept {
    g_store.store(store, std::memory_order_release);
}

std::span<const LumaServiceProvider* const> providers(void (*)(const char*)) noexcept {
    return kProviders;
}

std::optional<galaxy::assets::Entry> provided_find(std::string_view path) {
    try {
        return provided_at(path);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<galaxy::assets::Entry> provided_entry(std::int32_t index) {
    if (!is_provided(index)) {
        return std::nullopt;
    }
    Table& t = table();
    const std::shared_lock lock(t.lock);
    const Node* const node = node_at(t, index);
    return node != nullptr ? std::optional<galaxy::assets::Entry>(entry_of(*node, index)) : std::nullopt;
}

std::optional<galaxy::assets::Entry> provided_child(std::int32_t folder, std::uint32_t child) {
    if (!is_provided(folder)) {
        return std::nullopt;
    }
    Table& t = table();
    const std::shared_lock lock(t.lock);
    const Node* const node = node_at(t, folder);
    if (node == nullptr || !node->folder || child >= node->children.size()) {
        return std::nullopt;
    }
    const std::int32_t index = node->children[child];
    return entry_of(*node_at(t, index), index);
}

const std::uint8_t* provided_view(std::int32_t index) noexcept {
    if (!is_provided(index)) {
        return nullptr;
    }
    Table& t = table();
    const std::shared_lock lock(t.lock);
    const Node* const node = node_at(t, index);
    return node != nullptr ? node->block + node->position : nullptr;
}

bool provided_read(std::int32_t index, std::uint64_t offset, std::uint32_t size, void* dst) noexcept {
    if (!is_provided(index)) {
        return false;
    }
    Table& t = table();
    const std::shared_lock lock(t.lock);
    const Node* const node = node_at(t, index);
    if (node == nullptr || offset > node->length || node->length - offset < size) {
        return false;
    }
    if (size != 0u) {
        std::memcpy(dst, node->block + node->position + offset, size);
    }
    return true;
}

}  // namespace galaxy::luma_assets
