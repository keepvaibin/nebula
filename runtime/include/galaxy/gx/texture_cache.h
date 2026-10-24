#pragma once

// TextureCache: decodes GX textures from guest memory into GPU textures and serves their descriptors to the
// per-draw texture table.
//
// Entries are dropped only explicitly: on GXInvalidateTexAll() (FifoSink::on_invalidate_textures ->
// invalidate_all()), on an EFB copy over an overlapping guest range (register_efb_copy), and on a guest write
// overlapping a cached range (invalidate_guest_range), which drops decoded entries and EFB-copy aliases alike
// because later CPU-written buffers can reuse those addresses. For a texture matching an EFB-copy alias's
// address, size and format, get() returns the GPU-side copy; its guest bytes are never decoded.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/guest_range_envelope.h"
#include "galaxy/gx/texture_backend.h"
#include "galaxy/gx/shader_keys.h"  // fnv1a64
#include "galaxy/gx/texture_decode_pool.h"
#include "galaxy/gx/texture_pack.h"
#include "galaxy/gx/movie_replacement.h"
#include "galaxy/guest_memory.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

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
// C4/C8 only. Caller validates tiled source and the full 16/256-entry palette;
// palette points at the selected slot's big-endian entries.
void decode_small_index_tiles(TexFormat format, const std::uint8_t* src,
                              std::uint32_t width, std::uint32_t height,
                              const std::uint8_t* palette, TlutFormat tlut_format,
                              RGBA8* out);
// C14X2: caller validates block-rounded source and the full 16384-entry palette.
// Top two index bits are ignored; edge tiles still consume all 32 source bytes.
void decode_c14x2_tiles(const std::uint8_t* src,
                       std::uint32_t width, std::uint32_t height,
                       const std::uint8_t* palette, TlutFormat tlut_format,
                       RGBA8* out);
}  // namespace detail

[[nodiscard]] std::uint8_t efb_copy_alias_texture_format(
    std::uint8_t copy_format);

// GX block-rounded guest footprint of `guest_levels` contiguous authored levels (1 for a generated chain).
// Throws on invalid dimensions, formats or level counts.
[[nodiscard]] std::uint32_t texture_guest_byte_size(
    TexFormat format, std::uint32_t width, std::uint32_t height,
    unsigned guest_levels = 1u);
// Bounding span of the actual tiled block rows written by a texture copy.
// Includes stride gaps for conservative invalidation, but does not imply that
// a strided/subregion copy can be sampled as a packed GPU texture alias.
[[nodiscard]] std::uint32_t efb_copy_guest_byte_size(const EfbCopyParams& params);

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
    // A texture-pack replacement: width/height stay the guest texture's, the resource is larger by
    // 2^(lod_shift_x16 / 16) and samplers shift their LOD range by that much.
    std::uint8_t lod_shift_x16 = 0;
    bool lod_arbitrary = false;  // its levels pair with the guest's by index (pack_sampler_key)
    std::uint32_t guest_byte_size = 0;
    // The ray tracer's material table entry of its base level (rt_materials.h; 0: none), and that level's
    // texture-pack key (0: not computed, an EFB copy).
    std::uint16_t rt_material = 0;
    std::uint64_t pack = 0;
};

class TextureCache {
public:
    struct GuestRange {
        std::uint32_t guest_addr = 0;
        std::uint32_t size = 0;
    };

    bool initialize(gpu::Device* device);
    bool preallocate_upload_arenas(unsigned frames_in_flight);
    void shutdown();

    // The GPU texture for the image registers, decoded from guest memory on first use. Palettized formats use
    // the TLUT at `tlut.tmem_offset`. Throws on an unmapped guest range (reported as GxFatalError).
    [[nodiscard]] TextureHandle get(
        const TexImage& image,
        const TexMode& mode,
        const TlutRef& tlut,
        GuestMemory* memory);
    // Render thread only: marks a cached handle used (LRU) without reading guest memory. Retired and EFB-alias
    // handles are ignored.
    void touch_cached_handle(const TextureHandle& handle);

    // BP 0x64/0x65: copies a palette from guest memory into the TLUT bank (TMEM). Returns whether bytes changed;
    // retires only textures whose own palette bytes changed, so bindings survive identical reloads.
    [[nodiscard]] bool load_tlut(
        std::uint32_t src_reg,
        std::uint32_t dest_reg,
        GuestMemory* memory);

    // Aliases an EFB copy-to-texture at `guest_addr`, retiring every overlapping decoded entry and older copy.
    [[nodiscard]] bool register_efb_copy(
        std::uint32_t guest_addr,
        const EfbCopyParams& params,
        gpu::TextureRef texture);

    // GXInvalidateTexAll(): drop every decoded entry. EFB-copy aliases survive, because their
    // content lives on the GPU, not in guest memory.
    void invalidate_all();

    std::size_t invalidate_guest_range(
        std::uint32_t guest_addr,
        std::uint32_t size);
    std::size_t invalidate_guest_ranges(
        std::span<const GuestRange> sorted_coalesced_ranges);

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
    [[nodiscard]] std::uint64_t decoded_entry_guest_bytes() const noexcept {
        return decoded_guest_bytes_;
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
    struct Key {
        std::uint32_t guest_addr;
        std::uint16_t width;
        std::uint16_t height;
        std::uint8_t format;
        std::uint8_t tlut_format;
        std::uint16_t tlut_offset;
        std::uint8_t levels;        // decoded mip chain length (1 = no mips)
        std::uint8_t generated_mips; // lower levels generated from mip0
        std::uint8_t pad[2];        // keep the byte-wise hash deterministic

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
        std::uint32_t guest_addr;
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
                                               std::uint32_t guest_byte_size);
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
    detail::ConservativeGuestRangeEnvelope decoded_guest_envelope_;
    // LRU order of entries_ for the decoded budget (front: most recently used).
    std::list<Key> decoded_lru_;
    std::unordered_map<Key, std::list<Key>::iterator, KeyHasher>
        decoded_lru_pos_;
    std::uint64_t decoded_resident_bytes_ = 0;
    std::uint64_t decoded_guest_bytes_ = 0;
    std::uint64_t decoded_evicted_bytes_ = 0;
    std::uint64_t decoded_evicted_entries_ = 0;
    // Full GX texture descriptor -> EFB-copy alias (checked before decode).
    std::unordered_map<EfbAliasKey, Entry, EfbAliasKeyHasher> efb_aliases_;
    detail::ConservativeGuestRangeEnvelope alias_guest_envelope_;
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
    std::array<std::uint32_t, 3> movie_addr_{};
    std::array<std::uint16_t, 3> movie_guest_width_{};
    std::array<std::uint16_t, 3> movie_guest_height_{};
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

    // All of TMEM, so every TLUT slot and the largest transfer or palette fit. Entries stay big-endian.
    static constexpr std::size_t kTlutBankBytes = kTmemBytes;
    std::uint8_t tlut_bank_[kTlutBankBytes]{};
};

}  // namespace galaxy::gx
