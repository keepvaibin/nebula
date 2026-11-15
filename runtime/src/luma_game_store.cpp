// galaxy/luma_game_store.h: the validator's view of the game's own files.
#include "galaxy/luma_game_store.h"

#include "galaxy/asset_store.h"

#include <cstring>
#include <optional>
#include <string_view>

namespace galaxy {

namespace {

// The NTBL a cooked table starts with (cook/table.rs MAGIC).
constexpr char kTableMagic[4] = {'N', 'T', 'B', 'L'};

}  // namespace

LumaGameStoreView::LumaGameStoreView(const assets::AssetStore& store) : assets_(store) {
    store_.size = sizeof(LumaGameStore);
    store_.context = this;
    store_.path = &LumaGameStoreView::path;
    store_.table = &LumaGameStoreView::table;
    store_.folder = &LumaGameStoreView::folder;
}

std::string LumaGameStoreView::game_case(const char* path) const {
    if (path == nullptr) return {};
    std::string_view rest(path);
    while (!rest.empty() && rest.front() == '/') rest.remove_prefix(1);
    const std::string stored = assets::store_path(rest);
    // As PakStore's lookup: whole steps only, the game's own file or folder at each.
    if (stored.empty() || stored.back() == '/') return {};
    std::string out;
    std::string prefix;
    for (std::string_view left = stored;;) {
        const std::size_t slash = left.find('/');
        const std::string_view step = left.substr(0, slash);
        if (step.empty() || step == "." || step == "..") return {};
        if (!prefix.empty()) prefix += '/';
        prefix.append(step);
        const std::optional<assets::Entry> entry = assets_.find_game(prefix);
        if (!entry || entry->name == nullptr) return {};
        if (!out.empty()) out += '/';
        out += entry->name;
        if (slash == std::string_view::npos) break;
        left.remove_prefix(slash + 1u);
    }
    return out;
}

std::uint32_t LumaGameStoreView::path(void* context, const char* lower, char* out, std::uint32_t out_size) {
    const std::string found = static_cast<const LumaGameStoreView*>(context)->game_case(lower);
    if (found.empty() || found.size() >= UINT32_MAX) return 0;
    const auto length = static_cast<std::uint32_t>(found.size());
    if (out != nullptr && length < out_size) {
        std::memcpy(out, found.data(), found.size());
        out[found.size()] = '\0';
    }
    return length;
}

std::uint32_t LumaGameStoreView::table(void* context, const char* path, const std::uint8_t** bytes,
                                       std::uint64_t* bytes_size, const char** store_path) {
    auto* const self = static_cast<LumaGameStoreView*>(context);
    if (bytes == nullptr || bytes_size == nullptr) return 0;
    *bytes = nullptr;
    *bytes_size = 0;
    if (store_path != nullptr) *store_path = nullptr;
    std::string found = self->game_case(path);
    if (found.empty()) return 0;
    const std::optional<assets::Entry> entry = self->assets_.find_game(found);
    if (!entry || entry->folder || entry->length < sizeof(kTableMagic) ||
        static_cast<std::uint64_t>(static_cast<std::size_t>(entry->length)) != entry->length) {
        return 0;
    }
    const std::uint8_t* const mapped = self->assets_.map(entry->position, static_cast<std::size_t>(entry->length));
    // A file the store cannot give is no table here, as PakStore's read error is.
    if (mapped == nullptr || std::memcmp(mapped, kTableMagic, sizeof(kTableMagic)) != 0) return 0;
    self->table_path_ = std::move(found);
    *bytes = mapped;
    *bytes_size = entry->length;
    if (store_path != nullptr) *store_path = self->table_path_.c_str();
    return 1;
}

std::int32_t LumaGameStoreView::folder(void* context, const char* lower, LumaFolderSink sink, void* sink_context) {
    const auto* const self = static_cast<const LumaGameStoreView*>(context);
    if (lower == nullptr || sink == nullptr) return -1;
    std::string_view asked(lower);
    while (!asked.empty() && asked.front() == '/') asked.remove_prefix(1);
    while (!asked.empty() && asked.back() == '/') asked.remove_suffix(1);
    std::optional<assets::Entry> entry;
    if (asked.empty()) {
        entry = self->assets_.entry(0);
    } else {
        const std::string found = self->game_case(std::string(asked).c_str());
        if (found.empty()) return -1;
        entry = self->assets_.find_game(found);
    }
    if (!entry || !entry->folder) return -1;
    std::int32_t count = 0;
    for (std::uint32_t i = 0; i < entry->children && count < INT32_MAX; i++) {
        const std::optional<assets::Entry> child = self->assets_.child(entry->index, i);
        if (!child || child->name == nullptr) continue;
        sink(sink_context, child->name, child->folder ? 1u : 0u);
        count++;
    }
    return count;
}

}  // namespace galaxy
