// SPDX-License-Identifier: GPL-3.0-only
// The import check of P-059: every texture record in a store (game.pak) judged by the runtime's detail::authored_levels,
// and the answer compared with the bit the cook's judge wrote (NEBULA_TEXTURE_AUTHORED_LEVELS, cook/texture.rs
// authored_levels). A record is found wherever it lies in a file (a model's textures, a picture file's, a layout's), by
// its magic, version and a size that fits.
//
//     galaxy_authored_levels_check <game.pak> [<game.pak> ...]
//
// It prints the records, those with levels, those judged and those drawn by hand of each store, and each record on which
// the two judges differ; exit 1 when any differs or a store cannot be read. It needs the game's data, so it is not a
// unit test; run it on each disc's store after the importer changes.

#include "galaxy/asset_store.h"
#include "galaxy/render/texture_store.h"

#include <nebula_cooked_texture.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using galaxy::render::detail::authored_levels;

namespace {

struct Counts {
    std::uint64_t records = 0u, with_levels = 0u, judged = 0u, authored = 0u, differ = 0u;
};

// The records in `bytes` (one file of the store).
void check_file(const std::uint8_t* bytes, std::uint64_t length, const char* name, Counts& counts) {
    constexpr std::size_t kMagic = sizeof(NEBULA_TEXTURE_MAGIC) - 1u;
    std::uint64_t at = 0u;
    while (at + NEBULA_TEXTURE_HEADER <= length) {
        const void* found = std::memchr(bytes + at, NEBULA_TEXTURE_MAGIC[0], static_cast<std::size_t>(length - at));
        if (found == nullptr) {
            return;
        }
        at = static_cast<std::uint64_t>(static_cast<const std::uint8_t*>(found) - bytes);
        if (at + NEBULA_TEXTURE_HEADER > length || std::memcmp(bytes + at, NEBULA_TEXTURE_MAGIC, kMagic) != 0) {
            ++at;
            continue;
        }
        const NebulaTexture* const record = nebula_texture_record(bytes + at);
        if (record == nullptr || record->levels == 0u || record->levels > NEBULA_TEXTURE_MAX_LEVELS ||
            record->size > length - at ||
            NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(record->width, record->height, record->levels,
                                                               record->layout) > record->size) {
            ++at;
            continue;
        }
        ++counts.records;
        counts.with_levels += record->levels > 1u ? 1u : 0u;
        const bool generated = (record->flags & NEBULA_TEXTURE_GENERATED_LEVELS) != 0u;
        const bool cooked = (record->flags & NEBULA_TEXTURE_AUTHORED_LEVELS) != 0u;
        bool judged = false;
        if (!generated) {
            ++counts.judged;
            judged = authored_levels(record->layout, bytes + at + NEBULA_TEXTURE_HEADER, record->width, record->height,
                                     record->levels);
        }
        counts.authored += cooked ? 1u : 0u;
        if (judged != cooked) {
            ++counts.differ;
            std::printf("differ: %s at %llu: %ux%u, %u levels, layout %u: the cook says %s, the runtime %s\n", name,
                        static_cast<unsigned long long>(at), record->width, record->height, record->levels,
                        record->layout, cooked ? "drawn by hand" : "not", judged ? "drawn by hand" : "not");
        }
        at += record->size;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: galaxy_authored_levels_check <game.pak> [<game.pak> ...]\n");
        return 2;
    }
    bool ok = true;
    for (int i = 1; i < argc; ++i) {
        galaxy::assets::AssetStore store;
        if (!store.open(std::filesystem::path(argv[i]))) {
            std::printf("%s: not a store of version %u\n", argv[i], galaxy::assets::kStoreVersion);
            ok = false;
            continue;
        }
        Counts counts;
        for (std::int32_t index = 0;; ++index) {
            const auto entry = store.entry(index);
            if (!entry) {
                break;
            }
            if (entry->folder || entry->length < NEBULA_TEXTURE_HEADER) {
                continue;
            }
            const std::uint8_t* const bytes = store.map(entry->position, static_cast<std::size_t>(entry->length));
            if (bytes == nullptr) {
                std::printf("%s: %s cannot be read\n", argv[i], entry->name);
                ok = false;
                continue;
            }
            check_file(bytes, entry->length, entry->name, counts);
        }
        std::printf("%s (%s): %llu records, %llu with levels, %llu judged, %llu drawn by hand, %llu differ\n", argv[i],
                    std::string(store.game_id()).c_str(), static_cast<unsigned long long>(counts.records),
                    static_cast<unsigned long long>(counts.with_levels), static_cast<unsigned long long>(counts.judged),
                    static_cast<unsigned long long>(counts.authored), static_cast<unsigned long long>(counts.differ));
        ok = ok && counts.differ == 0u && counts.records != 0u;
    }
    return ok ? 0 : 1;
}
