// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's assets service (contract services-assets; plan 2.8), layer 1: `assets`, the game's files by path through
// the host's AssetStore (game.pak, with the mods' store over it). The store is mapped for the whole session and never
// changes once the game runs, so a call is a lookup in its index and a mapped view: nothing is copied, nothing waits.
#include "galaxy/luma_assets_service.h"

#include "galaxy/asset_store.h"

#include <luma_assets.h>
#include <nebula_game_api.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>
#include <string>

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
// mod path `<id>:<path>` (luma-format 5) is the mod's store folder Luma/<id>/<path> (mods.cpp plan_mod_changes).
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

std::optional<assets::Entry> look(const assets::AssetStore& store, std::string_view path, std::uint32_t flags) {
    return (flags & LUMA_ASSETS_GAME_ONLY) != 0u ? store.find_game(path) : store.find(path);
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
        const std::uint8_t* const mapped = store->map(found->position, static_cast<std::size_t>(found->length));
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
            found = store->child(folder->index, index);
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
            if (std::optional<assets::Entry> merged = store->find(full)) {
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

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaAssetsService kAssets = {
    {sizeof(LumaAssetsService), LUMA_ASSETS_MAJOR, LUMA_ASSETS_MINOR},
    assets_find,
    assets_bytes,
    assets_entry,
};

// assets reports only the registry's rows (100-101); it holds nothing for a mod, so it needs no mod's begin or end.
const LumaServiceProvider kAssetsProvider = {
    sizeof(LumaServiceProvider), LUMA_ASSETS_NAME, &kAssets.base, nullptr, nullptr, 0u, nullptr,
    nullptr, nullptr, nullptr, registered,
};
const LumaServiceProvider* const kProviders[] = {&kAssetsProvider};

}  // namespace

void set_store(const galaxy::assets::AssetStore* store) noexcept {
    g_store.store(store, std::memory_order_release);
}

std::span<const LumaServiceProvider* const> providers(void (*)(const char*)) noexcept {
    return kProviders;
}

}  // namespace galaxy::luma_assets
