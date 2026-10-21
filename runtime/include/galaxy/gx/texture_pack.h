// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Texture packs: higher-resolution replacements for the game's textures, found by the community naming convention
// (a data convention, implemented from its public description):
//   <pack>/[<GameID or its 3-letter prefix>/]**/tex1_<w>x<h>[_m]_<hash>[_<tlut hash>|_$]_<fmt>[_mip<n>|_arb].png|.dds
// <w>x<h> and <fmt> are the guest texture's size and GX format; <hash> the guest bytes of its base level, <tlut hash>
// the palette entries a C4/C8/C14X2 texture uses ($: any palette). A replacement is sampled with the guest texture's
// normalized coordinates at any size; its sampler LOD range shifts by log2 of the scale (TextureHandle::lod_shift_x16).
//
// TexturePacks owns the index of every pack folder (the player's pack and the mods' textures/ folders, a later folder
// winning a name; built on a loader thread), the loaders (PNG through stb_image, DDS BC1-BC7 through bcdec or 32-bit
// RGBA/BGRA, to RGBA8 levels, missing levels generated), the resident replacements (shared by the texture cache's
// entries, unused ones evicted under a VRAM budget) and the dump writer. The texture cache uploads finished loads
// through its own staging and backend (any GPU API). Render thread only, except where noted.

#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/texture_backend.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace galaxy::gx {

// The palette's hash: the TLUT entries from the lowest to the highest index the base level uses (2 bytes each, as
// stored in TMEM); 0 for direct formats. (The texture's own hash is texture_pack_hash, rt_materials.h.)
[[nodiscard]] std::uint64_t texture_pack_tlut_hash(TexFormat format, const std::uint8_t* base_level,
                                                   std::uint32_t width, std::uint32_t height,
                                                   const std::uint8_t* palette) noexcept;

struct TexturePackName {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t format = 0;
    std::uint8_t level = 0;   // _mip<n>
    bool mipmapped = false;   // _m
    bool any_tlut = false;    // _$
    bool arbitrary = false;   // _arb: levels with their own content, each shown where the guest shows that level
    std::uint64_t hash = 0;
    std::uint64_t tlut = 0;
};
// A file name's stem (no folder, no extension); nullopt when it is not a texture name.
[[nodiscard]] std::optional<TexturePackName> parse_texture_pack_name(std::string_view stem) noexcept;
// The stem a texture is dumped under (level 0; `level` adds _mip<n>).
[[nodiscard]] std::string texture_pack_name(const TexturePackName& name);
[[nodiscard]] bool texture_format_palettized(TexFormat format) noexcept;

// A DDS file's layout: levels are contiguous after the header(s).
enum class DdsFormat : std::uint8_t { Unknown, BC1, BC2, BC3, BC4, BC5, BC7, RGBA8, BGRA8 };
struct DdsLayout {
    DdsFormat format = DdsFormat::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t levels = 0;
    std::array<std::size_t, 16> level_offset{};
    std::array<std::uint32_t, 16> row_bytes{};  // bytes of one row of blocks (or pixels)
    std::array<std::uint32_t, 16> rows{};       // rows of blocks (or pixels)
};
// BC1-BC5 and BC7 (DXT1/DXT3/DXT5/ATI1/ATI2/BC4U/BC5U or a DX10 header; sRGB forms as UNORM), DX10 R8G8B8A8 and
// B8G8R8A8, DX9 A8R8G8B8 and A8B8G8R8. False for anything else or a truncated file.
[[nodiscard]] bool parse_dds(std::span<const std::uint8_t> file, DdsLayout& out) noexcept;

// An image as RGBA8 levels (rows tightly packed, 4 bytes a texel): a PNG (stb_image) or a DDS (its levels decoded;
// generated below them when it has one). False when the file is unreadable or not a supported image.
struct TexturePackPixels {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::vector<std::uint8_t>> levels;
    // Staged by a loader thread (TexturePacks::set_stager): the levels in an upload buffer, in the texture cache's
    // layout (offsets and row pitches by level); `levels` is then empty and level_count gives their number.
    std::optional<TextureBackend::UploadChunk> staging;
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint32_t> pitches;
    std::uint32_t level_count = 0;
};
// The upload layout of RGBA8 levels: each level at a placement-aligned offset, rows at pitch-aligned strides.
// Returns the total size.
std::uint64_t texture_pack_staging_layout(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                          std::vector<std::uint64_t>& offsets, std::vector<std::uint32_t>& pitches);
[[nodiscard]] bool load_texture_pack_image(const std::filesystem::path& file, bool dds, TexturePackPixels& out,
                                           std::string* error);
// RGBA8 rows (tightly packed) as a PNG file (stb_image_write); false when it could not be written.
bool write_png(const std::filesystem::path& file, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba);

struct TexturePackSettings {
    bool enabled = true;
    bool preload = false;
    bool dump = false;
    std::vector<std::filesystem::path> packs;  // pack folders, a later one winning a name
    std::filesystem::path dumps;               // dumps go to <dumps>/<GameID>
    std::uint64_t budget_bytes = 0;
    bool operator==(const TexturePackSettings&) const = default;
};

// A GPU-resident replacement: the cache binds `texture` in place of the decoded guest texture.
struct TextureReplacement {
    gpu::TextureRef texture;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t levels = 0;
    std::uint8_t lod_shift_x16 = 0;  // 16 x log2(replacement width / guest width)
    bool arbitrary = false;          // _arb: its level n shows where the guest shows its level n
    std::uint64_t bytes = 0;
};

class TexturePacks {
public:
    static constexpr std::uint32_t kNone = ~0u;
    static constexpr const char* kGameId = "RMGE01";

    TexturePacks() = default;
    TexturePacks(const TexturePacks&) = delete;
    TexturePacks& operator=(const TexturePacks&) = delete;
    ~TexturePacks() { shutdown(); }

    // Applies changed settings: new folders or enabling rebuild the index on a loader thread.
    void configure(const TexturePackSettings& settings);
    // Upload buffers for the loaders (any thread): finished loads arrive staged, so the render thread only records
    // their copies. Without one (tests), loads arrive as levels the caller stages.
    void set_stager(std::function<TextureBackend::UploadChunk(std::uint64_t)> stager) { stager_ = std::move(stager); }
    void shutdown() noexcept;

    [[nodiscard]] bool enabled() const noexcept { return settings_.enabled && index_ != nullptr; }
    [[nodiscard]] bool dumping() const noexcept { return settings_.dump && !settings_.dumps.empty(); }
    // The index has names for this guest size and format (hashing is needed only then).
    [[nodiscard]] bool may_match(std::uint32_t width, std::uint32_t height, TexFormat format) const noexcept;
    // The replacement slot for a texture's names, or kNone.
    [[nodiscard]] std::uint32_t find(std::uint32_t width, std::uint32_t height, TexFormat format, std::uint64_t hash,
                                     std::uint64_t tlut, bool mipmapped = false) const;
    // Null until the slot's texture is on the GPU.
    [[nodiscard]] const TextureReplacement* resident(std::uint32_t slot) const noexcept;
    // The file a slot loads from (traces).
    [[nodiscard]] std::filesystem::path file(std::uint32_t slot) const;
    // A cache entry binds (or waits for) the slot: loads it if needed and keeps it resident.
    void add_user(std::uint32_t slot);
    void remove_user(std::uint32_t slot) noexcept;

    void begin_frame() noexcept { ++frame_; }
    // Once per frame: publishes a finished index (returns true: every cached texture must be looked up again) and,
    // when `uploads` is empty, hands out finished loads to upload (slot and pixels, up to a byte budget). The cache
    // uploads them over the next frames (uploading()) and calls make_resident for each.
    bool pump(std::vector<std::pair<std::uint32_t, TexturePackPixels>>& uploads);
    // A slot pump handed out and not yet made resident (false for one of an index replaced since).
    [[nodiscard]] bool uploading(std::uint32_t slot) const noexcept;
    // The uploaded replacement of a slot pump handed out.
    void make_resident(std::uint32_t slot, TextureReplacement&& replacement);

    // Dump mode: writes a texture's base level once as <dumps>/<GameID>/<name>.png (on a loader thread).
    void dump(const TexturePackName& name, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba);

    struct Stats {
        std::uint64_t names = 0;
        std::uint64_t files = 0;
        std::uint64_t folders = 0;
        std::uint64_t levels = 0;
        std::uint64_t unreadable = 0;
        std::uint64_t loads = 0;
        std::uint64_t load_failures = 0;
        std::uint64_t uploads = 0;
        std::uint64_t upload_bytes = 0;
        std::uint64_t resident_bytes = 0;
        std::uint64_t resident = 0;
        std::uint64_t evictions = 0;
        std::uint64_t deferred = 0;
        std::uint64_t dumped = 0;
        double index_ms = 0.0;
        double load_ms = 0.0;  // loader thread time of finished loads
    };
    [[nodiscard]] Stats stats() const noexcept;

    // The last index's census for the settings menu (any thread): textures, custom levels, names not understood.
    struct Summary {
        bool indexed = false;
        std::uint64_t textures = 0;
        std::uint64_t levels = 0;
        std::uint64_t folders = 0;
        std::vector<std::string> unreadable;
    };
    [[nodiscard]] static Summary summary();

    // The index of these folders (tests: synchronous).
    struct Record {
        TexturePackName name;
        std::filesystem::path file;
        bool dds = false;
        std::vector<std::filesystem::path> mips;  // custom lower levels (_mip<n>) by level; empty where absent
    };
    struct Index {
        std::vector<Record> records;
        std::unordered_set<std::uint64_t> sizes;                  // (w, h, fmt) with any name
        std::unordered_map<std::uint64_t, std::uint32_t> exact;   // name key -> record
        std::unordered_map<std::uint64_t, std::uint32_t> any_tlut;
        std::uint64_t files = 0;
        std::uint64_t folders = 0;
        std::uint64_t levels = 0;     // custom lower levels (_mip<n>) of the textures
        std::uint64_t shadowed = 0;   // names an earlier pack also has (the later pack's file is used)
        std::uint64_t corrected = 0;  // names read past a slip ("_14v")
        std::vector<std::string> unreadable;  // tex1_ files whose names are not texture names
        double ms = 0.0;
        std::uint64_t generation = 0;
    };
    [[nodiscard]] static std::shared_ptr<Index> build_index(std::span<const std::filesystem::path> packs,
                                                            std::uint64_t generation);

private:
    struct Load {
        std::uint32_t slot = kNone;
        std::uint64_t generation = 0;
        TexturePackPixels pixels;
        double ms = 0.0;
        bool failed = false;
    };
    enum class SlotState : std::uint8_t { Idle, Loading, Ready, Uploading, Resident, Failed };
    struct Slot {
        SlotState state = SlotState::Idle;
        std::uint32_t users = 0;
        std::uint64_t last_use = 0;
        TextureReplacement replacement;
        std::unique_ptr<Load> ready;
    };

    static std::uint64_t size_key(std::uint32_t width, std::uint32_t height, std::uint32_t format) noexcept;
    static std::uint64_t name_key(std::uint64_t size, std::uint64_t hash, std::uint64_t tlut, bool mipmapped) noexcept;
    std::unique_ptr<Load> load(const Record& record, std::uint32_t slot, std::uint64_t generation) const;
    void request(std::uint32_t slot);
    void evict_to_budget(std::uint64_t incoming);
    void clear_slots() noexcept;
    void publish_summary() const;
    void start_workers();
    void submit(std::function<void()> job);
    void worker_main();
    void stop_workers() noexcept;

    TexturePackSettings settings_;
    std::function<TextureBackend::UploadChunk(std::uint64_t)> stager_;
    std::shared_ptr<const Index> index_;
    std::vector<Slot> slots_;
    std::deque<std::uint32_t> ready_queue_;
    std::uint32_t outstanding_ = 0;  // slots loading or waiting for their upload
    std::uint32_t preload_cursor_ = 0;
    std::uint64_t preload_start_ns_ = 0;  // steady clock at the index swap; 0 once the preload is reported
    bool configured_ = false;
    bool changed_ = false;  // the cache must look its textures up again
    std::uint64_t generation_ = 0;
    std::uint64_t frame_ = 0;
    std::uint64_t resident_bytes_ = 0;
    std::uint64_t resident_count_ = 0;
    std::uint64_t uploads_ = 0;
    std::uint64_t upload_bytes_ = 0;
    std::uint64_t evictions_ = 0;
    std::uint64_t deferred_ = 0;
    std::uint64_t loads_ = 0;
    std::uint64_t load_failures_ = 0;
    double load_ms_ = 0.0;
    std::unordered_set<std::string> dumped_;

    // Shared with the loader threads (mutex_).
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::unique_ptr<Load>> finished_;
    std::shared_ptr<Index> built_;
    std::uint64_t dumped_count_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace galaxy::gx
