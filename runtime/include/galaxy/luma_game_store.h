// The validator's view of the player's game (validator contract, revision 3 item 8; plan 3.12): a LumaGameStore over the
// game's own files in an AssetStore, never a mod's. It answers as LumaLink's store of an imported game.pak does
// (crates/nebula-import/src/store_view.rs PakStore), so the game's check and LumaLink's find the same:
//   path       the store path, in the game's case, of a disc or store path in any case ("StageData/X/XScenario.arc/a.bcsv"
//              is "StageData/XScenario/a.bcsv"); none for the root, a "." or ".." step, an empty step or a trailing '/';
//   table      a file's bytes when they are a cooked table (NTBL), mapped from the store, and its store path;
//   folder     the names directly in a folder, in the store's order (ScenarioDataParser's listing of /StageData).
// Which files may be new is the library's own list, the one LumaLink reads, so it is not part of the view.
// The AssetStore stays open and unchanged while the view is used; one thread at a time, as a report.
#pragma once

#include "luma_validate.h"

#include <cstdint>
#include <string>

namespace galaxy::assets {
class AssetStore;
}

namespace galaxy {

class LumaGameStoreView {
public:
    explicit LumaGameStoreView(const assets::AssetStore& store);
    LumaGameStoreView(const LumaGameStoreView&) = delete;
    LumaGameStoreView& operator=(const LumaGameStoreView&) = delete;

    [[nodiscard]] const LumaGameStore* get() const noexcept { return &store_; }
    // The store path in the game's case; empty when the game has none (as `path` gives 0).
    [[nodiscard]] std::string game_case(const char* path) const;

private:
    static std::uint32_t path(void* context, const char* lower, char* out, std::uint32_t out_size);
    static std::uint32_t table(void* context, const char* path, const std::uint8_t** bytes, std::uint64_t* bytes_size,
                               const char** store_path);
    static std::int32_t folder(void* context, const char* lower, LumaFolderSink sink, void* sink_context);

    const assets::AssetStore& assets_;
    LumaGameStore store_{};
    std::string table_path_;  // the last table's store path: valid until the next call, as the header says
};

}  // namespace galaxy
