#pragma once

// TextureCache: decodes GX textures from game memory into GPU textures and serves their descriptors to the
// per-draw texture table.
//
// Entries are dropped only explicitly: on GXInvalidateTexAll() (FifoSink::on_invalidate_textures ->
// invalidate_all()), on an EFB copy over an overlapping range (register_efb_copy), and on a guest write
// overlapping a cached range (invalidate_range), which drops decoded entries and EFB-copy aliases alike
// because later CPU-written buffers can reuse those addresses. For a texture matching an EFB-copy alias's
// address, size and format, get() returns the GPU-side copy; its source bytes are never decoded.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "galaxy/render/gx_bitfields.h"
#include "galaxy/render/range_envelope.h"
#include "galaxy/render/texture_backend.h"
#include "galaxy/render/shader_keys.h"  // fnv1a64
#include "galaxy/render/texture_decode_pool.h"
#include "galaxy/render/texture_pack.h"
#include "galaxy/render/movie_replacement.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

// A texture record (nebula_cooked_texture.h).
struct NebulaTexture;

namespace galaxy::render {

struct RGBA8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

namespace detail {
// Caller validates the complete 32-byte 4x4 tile footprint for RGB565/RGB5A3.
// Clipped edge tiles advance by 32 source bytes and write valid pixels only.
void decode_color_tiles(TexFormat format, const std::uint8_t* src,
    std::uint32_t width, std::uint32_t height, RGBA8* out);
// I4/I8/IA4/IA8 only. Caller validates the entire block-rounded source;
// clipped rows still advance over each complete 32-byte tile.
void decode_intensity_tiles(TexFormat format, const std::uint8_t* src,
                            std::uint32_t width, std::uint32_t height,
                            RGBA8* out);
// One level of a texture record (nebula_cooked_texture.h: its texels from `texels`, `width` x `height`) as RGBA8.
void expand_record_level(std::uint8_t layout, const std::uint8_t* texels, std::uint32_t width, std::uint32_t height,
                         RGBA8* out);
// One generated level for the enhanced-mipmaps option: a premultiplied-alpha 2x2 box filter with fixed
// rounding. `source` and `dest` must be distinct.
void generate_rgba8_mip_level(const std::vector<RGBA8>& source,
                             std::uint32_t source_width, std::uint32_t source_height,
                             std::vector<RGBA8>& dest,
                             std::uint32_t dest_width, std::uint32_t dest_height);
// A level of at least 4x4 texels that alternates two values on every texel is a dither the game painted for
// the Wii's 6-bit framebuffer (the sky's DotNoise, the white and red plastics). In 8-bit colour it only shows,
// magnified, as a grid of dots; it becomes the two values' rounded average. Returns whether it was one.
bool flatten_texel_checker(std::vector<RGBA8>& pixels, std::uint32_t width, std::uint32_t height);
// A level whose texel-scale detail is a one-texel checker over its content: the art's ordered dither, baked in before
// the texture was compressed (lawns, turf, sand, concrete, bed cloth). Detected where at least a fifth of the texels
// differ from their four neighbours' mean by 2 levels or more and the sign of that difference follows one checker
// phase in at least 95 % of them. Each colour channel loses the checker at its local amplitude (the 3x3 mean of the
// difference against the checker); the content and alpha stay. Returns whether it was one.
bool remove_texel_checker(std::vector<RGBA8>& pixels, std::uint32_t width, std::uint32_t height);
// Caller validates the complete block-rounded source footprint. Edge tiles
// advance by all 64 source bytes but write only pixels inside width/height.
void decode_rgba8_tiles(const std::uint8_t* src, std::uint32_t width,
                        std::uint32_t height, RGBA8* out);
// Caller validates the complete block-rounded CMPR footprint. Subblocks keep
// GX selector order, 5/3 interpolation and averaged RGB for alpha-zero pixels.
void decode_cmpr_tiles(const std::uint8_t* src, std::uint32_t width,
                       std::uint32_t height, RGBA8* out);
}  // namespace detail

[[nodiscard]] std::uint8_t efb_copy_alias_texture_format(
    std::uint8_t copy_format);

// GX block-rounded guest footprint of `source_levels` contiguous authored levels (1 for a generated chain).
// Throws on invalid dimensions, formats or level counts.
[[nodiscard]] std::uint32_t texture_source_bytes(
    TexFormat format, std::uint32_t width, std::uint32_t height,
    unsigned source_levels = 1u);
// Bounding span of the actual tiled block rows written by a texture copy.
// Includes stride gaps for conservative invalidation, but does not imply that
// a strided/subregion copy can be sampled as a packed GPU texture alias.
[[nodiscard]] std::uint32_t efb_copy_source_bytes(const EfbCopyParams& params);

// What a draw needs to bind one texture map.
struct TextureHandle {
    gpu::TextureObject* resource = nullptr;
    // Index into the cache's CPU-side SRV heap; the renderer stages it into
    // the frame's shader-visible table.
    std::uint32_t srv_index = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t mip_levels = 1;
    bool from_efb_copy = false;
    bool generated_mips = false;
    // A texture-pack replacement: width/height stay the game texture's, the resource is larger by
    // 2^(lod_shift_x16 / 16) and samplers shift their LOD range by that much.
    std::uint8_t lod_shift_x16 = 0;
    bool lod_arbitrary = false;  // its levels pair with the game's by index (pack_sampler_key)
    std::uint32_t source_bytes = 0;
    // The ray tracer's material table entry of its base level (rt_materials.h; 0: none), and that level's
    // texture-pack key (0: not computed, an EFB copy).
    std::uint16_t rt_material = 0;
    std::uint64_t pack = 0;
};

class TextureCache {
public:
    struct MemoryRange {
        std::uint64_t address = 0;
        std::uint32_t size = 0;
    };

    bool initialize(gpu::Device* device);
    bool preallocate_upload_arenas(unsigned frames_in_flight);
    void shutdown();

    // The GPU texture for an image, uploaded on first use from the texture record the game handed over, or decoded
    // from the image's bytes (register_image; one never handed over decodes from zeros). Colour-indexed images come
    // as records with their palettes applied: `palette_address` and `palette_format` are not read.
    [[nodiscard]] TextureHandle get(
        const TexImage& image,
        const TexMode& mode,
        std::uint64_t palette_address,
        TlutFormat palette_format);
    // Render thread only: marks a cached handle used (LRU) without reading game memory. Retired and EFB-alias
    // handles are ignored.
    void touch_cached_handle(const TextureHandle& handle);

    // Aliases an EFB copy-to-texture at `address`, retiring every overlapping decoded entry and older copy.
    [[nodiscard]] bool register_efb_copy(
        std::uint64_t address,
        const EfbCopyParams& params,
        gpu::TextureRef texture);

    // A target pass's picture (E-07) at `address`, `width` x `height` in the game's units: a draw sampling that
    // address at that size reads it whatever format its texture object names (the picture at full precision).
    // Replaces an earlier picture there; the texture is the cache's from now on.
    // `channels` (NebulaTargetChannels) picks the channels the samplers read (texture_backend write_view swizzle).
    void register_target(std::uint64_t address, std::uint16_t width, std::uint16_t height, gpu::TextureRef texture,
                         std::uint8_t channels = 0u);

    // GXInvalidateTexAll(): drop every decoded entry. EFB-copy aliases survive, because their
    // content lives on the GPU, not in game memory.
    void invalidate_all();

    // An image's bytes the game handed over (texture_image): textures at `address` decode from this copy from now
    // on; entries decoded from earlier bytes there are retired.
    void register_image(std::uint64_t address, std::shared_ptr<const std::vector<std::byte>> bytes);
    // Forgets the handed-over images overlapping the ranges (memory the game rewrote without handing it over).
    void drop_registered_images(std::span<const MemoryRange> ranges);
    // Images and palettes sampled without having been handed over (each logged).
    [[nodiscard]] std::uint64_t missing_images() const noexcept { return missing_images_; }

    std::size_t invalidate_range(
        std::uint64_t address,
        std::uint32_t size);
    std::size_t invalidate_ranges(
        std::span<const MemoryRange> sorted_coalesced_ranges);

    // The GPU side: views by srv_index, upload staging, textures (texture_backend.h).
    [[nodiscard]] const TextureBackend& backend() const noexcept { return backend_; }
    [[nodiscard]] TextureBackend& backend() noexcept { return backend_; }
    // Render-thread-only lifetime epoch. Raw handles remain valid for this
    // frame, but cross-frame handle caches must drop them after retirement.
    [[nodiscard]] std::uint64_t retirement_revision() const noexcept {
        return retirement_revision_;
    }
    // Query at frame begin, before any new allocations can reuse reclaimed
    // slots. A reused descriptor is a new identity and clears its retired bit.
    [[nodiscard]] bool srv_index_retired(std::uint32_t index) const noexcept {
        return index >= kSrvHeapCapacity || retired_srv_indices_[index];
    }

    // Guest source bytes of the decoded entries (a diagnostic; the budget counts allocations).
    [[nodiscard]] std::uint64_t decoded_entry_source_bytes() const noexcept {
        return decoded_source_bytes_;
    }

    // Each live decoded entry is charged its committed allocation. Retired resources awaiting their fence are not
    // counted; this is not total VRAM residency.
    struct DecodedMapStats {
        std::uint64_t resident_bytes = 0;   // committed allocation bytes per live entry
        std::uint64_t budget_bytes = 0;
        std::uint64_t evicted_bytes = 0;
        std::uint64_t evicted_entries = 0;
        std::size_t entries = 0;
        std::size_t lru_entries = 0;
    };
    [[nodiscard]] DecodedMapStats decoded_map_stats() const noexcept;

    // Called after the renderer has waited for this frame slot's fence.
    void begin_frame(unsigned frame_slot, unsigned frames_in_flight);

    // Interpolated redraw replays a game frame's draws until the renderer's
    // game-frame ring segment comes round again. Called before begin_frame
    // with that segment (kNoHoldSegment outside interpolation): a reclaimed
    // slot's textures then keep their resources, out of the pool, until the
    // segment current at reclaim comes round (its fence has been waited on).
    // Their SRV slots are reclaimed as usual: replayed draws read descriptor
    // tables already copied into the renderer's shader-visible ring.
    static constexpr unsigned kNoHoldSegment = ~0u;
    static constexpr unsigned kMaxHoldSegments = 4u;
    void set_retirement_hold_segment(unsigned segment);
    [[nodiscard]] std::uint64_t take_held_texture_count() noexcept {
        const std::uint64_t count = held_texture_count_;
        held_texture_count_ = 0u;
        return count;
    }

    // Asynchronous first-use decode. While allowed, a miss creates the texture and records its staging copy and SRV
    // on this thread, and only the decode into the staging memory runs on a worker. The caller guarantees the guest
    // bytes passed to get() stay unchanged until finish_pending_decodes() (true for a sealed frame memory snapshot),
    // which must run before the upload command list executes and rethrows the first decode failure. Texture-pack
    // dumps, which read the decoded pixels, stay synchronous.
    void set_async_decode_allowed(bool allowed) noexcept {
        async_decode_allowed_ = allowed;
    }
    void finish_pending_decodes();
    // Unwinding-path join: never throws.
    void finish_pending_decodes_noexcept() noexcept;
    [[nodiscard]] std::uint64_t async_decode_jobs() const noexcept {
        return async_decode_jobs_;
    }

    // Texture packs (texture_pack.h). configure_texture_packs applies changed settings; once per frame after the
    // upload list is set, apply_texture_packs uploads finished replacements and swaps them in for the entries that
    // wait for them (through retire(), like an invalidation); a new index or a changed dump mode drops every decoded
    // entry so each texture is looked up again.
    void configure_texture_packs(const TexturePackSettings& settings) {
        if (!stager_set_) {
            // Before any load exists: the loader threads read it.
            packs_.set_stager([this](std::uint64_t size) { return backend_.create_upload_chunk(size); });
            stager_set_ = true;
        }
        packs_.configure(settings);
    }
    void apply_texture_packs();

    [[nodiscard]] TexturePacks::Stats texture_pack_stats() const noexcept { return packs_.stats(); }
    // HD movie replacements (movie_replacement.h): `frame`'s planes in place of the game's movie planes (the I8
    // textures at the mark's addresses) until the next call; null draws the game's own. Uploads through the current
    // upload list. True when what those addresses resolve to changed.
    bool set_movie_override(const MovieFrameMark& mark, const MovieReplacementFrame* frame);
    [[nodiscard]] bool movie_override_active() const noexcept { return movie_active_; }
    // A descriptor slot of a replacement plane (handles naming it change with each frame).
    [[nodiscard]] bool movie_handle(std::uint32_t srv_index) const noexcept;

private:
    // Handed-over images by address, and copies replaced while a decode may still read them (released at the next
    // finish_pending_decodes).
    std::map<std::uint64_t, std::shared_ptr<const std::vector<std::byte>>> registered_images_;
    std::vector<std::shared_ptr<const std::vector<std::byte>>> replaced_images_;
    std::uint64_t missing_images_ = 0;
    [[nodiscard]] const std::byte* registered_bytes(std::uint64_t address, std::uint32_t size) const;
    // The texture record handed over at exactly `address` (nebula_cooked_texture.h), when that is what it is.
    [[nodiscard]] const ::NebulaTexture* registered_record(std::uint64_t address) const;

    struct Key {
        std::uint64_t address;
        std::uint16_t width;
        std::uint16_t height;
        std::uint8_t format;
        std::uint8_t levels;        // decoded mip chain length (1 = no mips)
        std::uint8_t generated_mips; // lower levels generated from mip0

        bool operator==(const Key&) const = default;
    };
    struct KeyHasher {
        [[nodiscard]] std::size_t operator()(const Key& key) const {
            return static_cast<std::size_t>(fnv1a64(&key, sizeof(key)));
        }
    };
    struct Entry {
        gpu::TextureRef texture;
        TextureHandle handle;
        std::uint64_t allocation_bytes = 0;
        // Its texture-pack replacement (bound, or awaited while the decoded original shows).
        std::uint32_t pack_slot = TexturePacks::kNone;
        bool pack_waiting = false;
    };
    struct EfbAliasKey {
        std::uint64_t address;
        std::uint16_t width;
        std::uint16_t height;
        std::uint8_t format;
        std::uint8_t pad[3];

        bool operator==(const EfbAliasKey&) const = default;
    };
    struct EfbAliasKeyHasher {
        [[nodiscard]] std::size_t operator()(const EfbAliasKey& key) const {
            return static_cast<std::size_t>(fnv1a64(&key, sizeof(key)));
        }
    };
    void retire(Entry&& entry);
    // An entry that binds a resident replacement (no decode; charges no decoded budget and is never pooled).
    [[nodiscard]] Entry make_replacement_entry(const TextureReplacement& replacement, std::uint32_t slot,
                                               std::uint16_t width, std::uint16_t height, bool generated_mips,
                                               std::uint32_t source_bytes);
    void wait_for_replacement(const Key& key, Entry& entry, std::uint32_t slot);
    // Decoded-map LRU: touch moves a key to the most recently used end, forget removes it; both ignore an absent key.
    void decoded_lru_touch(const Key& key);
    void decoded_lru_forget(const Key& key);
    // Removes a key from the LRU and its footprint from the resident total; every entries_ removal calls it.
    void decoded_lru_release(const Key& key, Entry& entry);
    // The inverse for a newly inserted entry, then evicts down to the budget.
    void decoded_lru_track(const Key& key);
    // Evicts oldest first down to the byte bound, through retire(): no resource is destroyed while a submitted
    // command list may still reference it.
    void decoded_lru_evict_to_budget();
    void release_upload_arenas();
    struct UploadAllocation {
        const TextureBackend::UploadChunk* chunk = nullptr;
        std::uint8_t* cpu_base = nullptr;
        std::uint64_t offset = 0;
    };
    struct UploadArenaFrame {
        std::vector<TextureBackend::UploadChunk> chunks;
        std::size_t active_chunk = 0;
        std::uint64_t cursor = 0;
    };
    void append_upload_arena_chunk(UploadArenaFrame& frame, std::uint64_t min_size);
    [[nodiscard]] UploadAllocation allocate_upload_bytes(
        std::uint64_t size,
        std::uint64_t alignment);
    [[nodiscard]] std::uint32_t allocate_srv_index();

    std::unordered_map<Key, Entry, KeyHasher> entries_;
    detail::ConservativeRangeEnvelope decoded_envelope_;
    // LRU order of entries_ for the decoded budget (front: most recently used).
    std::list<Key> decoded_lru_;
    std::unordered_map<Key, std::list<Key>::iterator, KeyHasher>
        decoded_lru_pos_;
    std::uint64_t decoded_resident_bytes_ = 0;
    std::uint64_t decoded_source_bytes_ = 0;
    std::uint64_t decoded_evicted_bytes_ = 0;
    std::uint64_t decoded_evicted_entries_ = 0;
    // Full GX texture descriptor -> EFB-copy alias (checked before decode).
    std::unordered_map<EfbAliasKey, Entry, EfbAliasKeyHasher> efb_aliases_;
    // Target passes' pictures by address (register_target), checked before the EFB-copy aliases.
    std::unordered_map<std::uint64_t, Entry> targets_;
    detail::ConservativeRangeEnvelope alias_envelope_;
    std::vector<std::vector<Entry>> retired_entries_;
    // Interpolation: reclaimed textures held per game-frame segment.
    std::array<std::vector<Entry>, kMaxHoldSegments> held_entries_;
    unsigned hold_segment_ = kNoHoldSegment;
    std::uint64_t held_texture_count_ = 0;
    void release_held_entries(std::vector<Entry>& held);
    // Recycled decoded-texture resources. Creating a committed resource on first use can stall the texture
    // flush, so a GPU-idle retired texture of the same size and mip count is reused instead. RGBA8 2D, no
    // flags, PSR state.
    struct PooledTexture {
        gpu::TextureRef texture;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t levels = 0;
        std::uint64_t allocation_bytes = 0;
    };
    std::vector<PooledTexture> texture_pool_;
    std::uint64_t texture_pool_bytes_ = 0;
    void pool_retired_texture(Entry& entry);
    bool take_pooled_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels,
        gpu::TextureRef& texture, std::uint64_t& allocation_bytes);
    std::vector<UploadArenaFrame> upload_arenas_;
    // Render-thread decode scratch, kept between misses to avoid reallocating.
    std::vector<std::vector<RGBA8>> level_pixels_scratch_;
    std::unique_ptr<TextureDecodePool> decode_pool_;
    TexturePacks packs_;
    // Entries waiting for each replacement slot (checked when the slot becomes resident).
    std::unordered_map<std::uint32_t, std::vector<Key>> pack_waiters_;
    std::vector<std::pair<std::uint32_t, TexturePackPixels>> pack_uploads_;
    // Loader-staged upload buffers by frame slot, released when the slot comes round again (its copies are done).
    std::vector<std::vector<TextureBackend::UploadChunk>> pack_staging_;
    bool stager_set_ = false;
    struct MoviePlane {
        gpu::TextureRef texture;
        std::uint32_t srv_index = ~0u;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool uploaded = false;
    };
    static constexpr unsigned kMovieRing = 4;  // frames a plane's texture rests before it is written again
    std::array<std::array<MoviePlane, 3>, kMovieRing> movie_ring_{};
    unsigned movie_ring_at_ = 0;
    bool movie_active_ = false;
    std::int32_t movie_frame_ = -1;
    const void* movie_staging_seen_ = nullptr;
    std::array<std::uint64_t, 3> movie_addr_{};
    std::array<std::uint16_t, 3> movie_source_width_{};
    std::array<std::uint16_t, 3> movie_source_height_{};
    std::array<TextureHandle, 3> movie_handles_{};
    // Upload buffers of the planes by frame slot, held until the slot comes round (the decoder then reuses them).
    std::vector<std::vector<std::shared_ptr<TextureBackend::UploadChunk>>> movie_staging_;
    [[nodiscard]] bool current_frame_slot_invalid() const noexcept { return current_frame_slot_ >= pack_staging_.size(); }
    std::uint64_t pack_hashed_ = 0;
    std::uint64_t pack_bound_ = 0;
    std::uint64_t pack_swapped_ = 0;
    std::uint64_t pack_stats_frame_ = 0;
    std::uint64_t pack_apply_ns_ = 0;      // render-thread time in apply_texture_packs (uploads, swaps)
    std::uint64_t pack_apply_max_ns_ = 0;  // its longest frame
    bool async_decode_allowed_ = false;
    bool async_decode_pending_ = false;
    std::uint64_t async_decode_jobs_ = 0;
    std::vector<std::uint64_t> level_offsets_scratch_;
    std::vector<std::uint32_t> level_pitches_scratch_;
    unsigned current_frame_slot_ = 0;

    // Decoded textures are placed in shared heaps (placed_texture_allocator.h).
    TextureBackend backend_;
    std::uint32_t next_srv_index_ = 0;
    std::vector<std::uint32_t> free_srv_indices_;
    // Heap size and retirement bitmap must describe the same index domain.
    static constexpr std::uint32_t kSrvHeapCapacity = 4096u;
    // Decoded key by SRV slot, for touch_cached_handle (fixed storage: no allocation on a cached bind).
    std::array<std::optional<Key>, kSrvHeapCapacity> decoded_srv_keys_{};
    std::bitset<kSrvHeapCapacity> retired_srv_indices_;
    // Kept monotonic across shutdown/reinitialization, like resource lifetimes.
    std::uint64_t retirement_revision_ = 0;

};

}  // namespace galaxy::render
