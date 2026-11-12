// The game's asset store (game.pak, crates/nebula-import/src/cook/store.rs): its files and folders by path, by index and by
// folder, and its bytes. Paths from the root, '/' returning to it, "." and ".." steps, letters matched in either case. Only
// the index stays in memory; any thread may read at once.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::assets {

struct Entry {
    std::int32_t index = -1;    // 0 is the root folder
    bool folder = false;
    bool archive = false;       // an archive's folder: one block, which a mods' store replaces whole
    std::uint8_t kind = 0;      // a file's NEBULA_ASSET_KIND_*
    std::uint64_t length = 0;   // a file's length in bytes; a folder's, from its first file to its last one's end
    std::uint64_t position = 0; // the first byte in the store
    std::uint32_t children = 0; // a folder's entries
    bool from_overlay = false;  // a mods' store's entry (attach_overlay), not the game's own
    const char* name = nullptr; // valid while the store is open
};

class AssetStore {
public:
    // Mods' cooked files over this store (cook/overlay.rs): found first, by the same paths. False when it cannot be read.
    bool attach_overlay(const std::filesystem::path& path);
    AssetStore() = default;
    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;
    ~AssetStore();

    // False when the file cannot be read or is not a store (it is then empty).
    bool open(const std::filesystem::path& path);
    // A store's whole image in memory, kept by the caller.
    bool load(const std::uint8_t* data, std::size_t size);
    // `size` bytes at `position` of the store.
    bool read(std::uint64_t position, std::size_t size, void* dst) const;
    // `size` bytes at `position` where they are mapped, read-only, while the store is open; the pages are on their
    // way in when this returns, so the first use does not wait for the drive. Null if out of range.
    [[nodiscard]] const std::uint8_t* map(std::uint64_t position, std::size_t size) const;
    // The same bytes as a view of their own, writable, the writes staying in this process's pages. Null when it cannot be
    // had. Ended by unmap_private, or kept while the store is open.
    [[nodiscard]] std::uint8_t* map_private(std::uint64_t position, std::size_t size) const;
    void unmap_private(void* data, std::size_t size) const;

    [[nodiscard]] std::optional<Entry> entry(std::int32_t index) const;
    [[nodiscard]] std::optional<Entry> find(std::string_view path) const;
    // The game's own file or folder at `path`, under any mod's.
    [[nodiscard]] std::optional<Entry> find_game(std::string_view path) const { return find_own(path); }
    // The index-th entry of a folder.
    [[nodiscard]] std::optional<Entry> child(std::int32_t folder, std::uint32_t index) const;
    // What the mods add to a folder of the game: `find` has the game's folder, so its children don't list these.
    [[nodiscard]] std::optional<Entry> added_child(std::string_view path, std::uint32_t index) const;
    [[nodiscard]] std::string_view game_id() const noexcept { return game_id_; }
    [[nodiscard]] unsigned revision() const noexcept { return revision_; }
    // The language folders at the root ("UsEnglish", ...): the game's in the store's order, then those the mods add.
    [[nodiscard]] std::vector<std::string> language_folders() const;

private:
    struct Node {
        std::uint64_t position;
        std::uint64_t size;
        std::uint32_t name;
        std::uint32_t parent;
        std::uint32_t flags;
        std::uint32_t first;
        std::uint32_t children;
        std::uint32_t reserved;
    };
    struct Hash {
        std::uint64_t hash;
        std::uint32_t node;
        std::uint32_t reserved;
    };

    void close();
    [[nodiscard]] std::optional<Entry> find_own(std::string_view path) const;
    [[nodiscard]] std::optional<Entry> find_merged(std::string_view path) const;
    bool parse(const std::uint8_t* header, const std::uint8_t* index, std::uint64_t index_at, std::uint64_t size);

    // Positions past this are the overlay's; indices past count_ are its nodes.
    static constexpr std::uint64_t kOverlayPosition = std::uint64_t{1} << 50u;
    std::unique_ptr<AssetStore> overlay_;
    // With mods: the game's own language folder (its English, else its first) that a language a mod adds falls back to.
    std::string language_fallback_;
    std::vector<std::uint8_t> index_;
    const std::uint8_t* image_ = nullptr;
    const std::uint8_t* view_ = nullptr;
    std::uint64_t size_ = 0;
#if defined(_WIN32)
    void* file_ = nullptr;
    void* mapping_ = nullptr;
#else
    int file_ = -1;
#endif
    const Node* nodes_ = nullptr;
    const Hash* hashes_ = nullptr;
    const char* names_ = nullptr;
    std::uint32_t count_ = 0;
    std::string game_id_;
    unsigned revision_ = 0;
};

// The store's name for a game or mod path (cook/store.rs store_path): an archive or compressed file loses its ".arc" or
// ".szs", and an archive in a stage's folder (StageData/<S>/<S>Scenario.arc) is in StageData. A store path maps to itself.
[[nodiscard]] std::string store_path(std::string_view path);

// The store's first bytes hold this.
inline constexpr char kStoreMagic[8] = {'N', 'B', 'S', 'T', 'O', 'R', 'E', '\0'};
// The store's contents' form (store_version.inc, which the importer writes): a store of another version needs
// importing again.
inline constexpr std::uint32_t kStoreVersion =
#include "store_version.inc"
    ;

}  // namespace galaxy::assets
