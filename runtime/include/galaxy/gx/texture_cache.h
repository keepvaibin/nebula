#pragma once

// TextureCache: decodes GX texture formats from guest memory into GPU
// textures and serves the descriptor used by the per-draw texture table.
//
// Address-cache invalidation is explicit. An opt-in bounded content cache may
// retain identical decoded static resources after those entries are dropped:
//   1. the parser reports the BP pattern emitted by GXInvalidateTexAll()
//      (FifoSink::on_invalidate_textures -> invalidate_all()), or
//   2. an EFB copy registers over an overlapping guest address range
//      (register_efb_copy overwrites conflicting aliases), or
//   3. the runtime reports a guest-memory write overlapping a cached texture
//      range (invalidate_guest_range).
// A guest write must invalidate both decoded RAM address entries and GPU-side
// EFB-copy aliases, because later CPU-decoded buffers can reuse the same guest
// addresses.
//
// EFB-copy aliases: once register_efb_copy() claims a matching guest address,
// size, and format, get() returns the GPU-side copy directly
// — the stale guest bytes are never re-decoded.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/guest_range_envelope.h"
#include "galaxy/gx/shader_keys.h"  // fnv1a64
#include "galaxy/native_api.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <list>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

class RendererD3D12;
class DependencyEventSink;

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
// Native enhancement only: preserve the premultiplied-alpha box filter and
// rounding of every output byte. Source and destination storage are distinct.
void generate_rgba8_mip_level(const std::vector<RGBA8>& source,
                             std::uint32_t source_width, std::uint32_t source_height,
                             std::vector<RGBA8>& dest,
                             std::uint32_t dest_width, std::uint32_t dest_height);
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

// GX block-rounded source footprint. Authored mip levels are contiguous;
// generated enhancement levels must pass guest_levels=1. Reject invalid
// dimensions/formats/level counts before any cache lookup or guest read.
[[nodiscard]] std::uint32_t texture_guest_byte_size(
    TexFormat format, std::uint32_t width, std::uint32_t height,
    unsigned guest_levels = 1u);
// Bounding span of the actual tiled block rows written by a texture copy.
// Includes stride gaps for conservative invalidation, but does not imply that
// a strided/subregion copy can be sampled as a packed GPU texture alias.
[[nodiscard]] std::uint32_t efb_copy_guest_byte_size(const EfbCopyParams& params);

// What a draw needs to bind one texture map.
struct TextureHandle {
    ID3D12Resource* resource = nullptr;
    // Index into the cache's CPU-side SRV heap; the renderer stages it into
    // the frame's shader-visible table.
    std::uint32_t srv_index = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t mip_levels = 1;
    bool from_efb_copy = false;
    bool generated_mips = false;
    std::uint32_t guest_byte_size = 0;
};

class TextureCache {
public:
    struct GuestRange {
        std::uint32_t guest_addr = 0;
        std::uint32_t size = 0;
    };

    bool initialize(ID3D12Device* device);
    bool preallocate_upload_arenas(unsigned frames_in_flight);
    void shutdown();

    // Returns the GPU texture for the given image registers, decoding from
    // guest memory on first use (I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/C4/C8/
    // C14X2/CMPR, untiled from 4x4/8x4/8x8 blocks).  Palettized formats
    // combine with the TLUT loaded at `tlut.tmem_offset`.  An unmapped guest
    // range is a GxFatalError (hard-fail).
    [[nodiscard]] TextureHandle get(
        const TexImage& image,
        const TexMode& mode,
        const TlutRef& tlut,
        GuestMemoryV1* memory);

    // BP 0x64/0x65: copy a palette from guest memory into the TMEM-modelled
    // TLUT bank. Return whether bytes changed; retire a texture only if bytes
    // in its own palette changed, including partial overlapping transfers.
    // Callers can retain bindings on identical reloads.
    [[nodiscard]] bool load_tlut(
        std::uint32_t src_reg,
        std::uint32_t dest_reg,
        GuestMemoryV1* memory);

    // EFB copy-to-texture landed at `guest_addr`: alias it.  Overwrites any
    // existing overlapping entry (decoded or previous copy).
    [[nodiscard]] bool register_efb_copy(
        std::uint32_t guest_addr,
        const EfbCopyParams& params,
        Microsoft::WRL::ComPtr<ID3D12Resource> texture);

    // GXInvalidateTexAll(): drop every decoded entry.  EFB-copy aliases
    // survive — their content lives on the GPU, not in guest memory.
    void invalidate_all();

    std::size_t invalidate_guest_range(
        std::uint32_t guest_addr,
        std::uint32_t size);
    std::size_t invalidate_guest_ranges(
        std::span<const GuestRange> sorted_coalesced_ranges);

    // Debug: treat every frame as invalidated (A/B fidelity testing).
    bool debug_invalidate_every_frame = false;

    [[nodiscard]] ID3D12DescriptorHeap* srv_heap() const;
    [[nodiscard]] UINT srv_descriptor_stride() const noexcept { return srv_stride_; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE null_srv() const noexcept { return null_srv_; }
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

    // Command list new-texture uploads are recorded on (CopyTextureRegion +
    // barrier to PIXEL_SHADER_RESOURCE).  Set by GxBackend each frame before
    // parsing; decoded textures are DEFAULT-heap Texture2D resources, so the
    // staged copy must ride the frame's command list.
    void set_upload_list(ID3D12GraphicsCommandList* list) {
        upload_list_ = list;
    }
    // Scoped to one opt-in render parse. Null in the normal rendering path.
    void set_dependency_event_sink(DependencyEventSink* sink) noexcept {
        dependency_event_sink_ = sink;
    }
    // Existing capture markers are bounded by the backend's requested frames.
    // Zero leaves normal cache behavior and logging unchanged.
    void set_debug_capture_frame(std::uint64_t frame) noexcept {
        debug_capture_frame_ = frame;
    }

    // Called after the renderer has waited for this frame slot's fence.
    void begin_frame(unsigned frame_slot, unsigned frames_in_flight);

private:
    DependencyEventSink* dependency_event_sink_ = nullptr;
    std::uint64_t debug_capture_frame_ = 0u;
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
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        TextureHandle handle;
    };
    // Adapted from Aurora's MIT-licensed TextureContentKey and bounded LRU:
    // encounter/aurora@77326d45415a64c40e560cebd2cdef0a0f08d840,
    // lib/gx/texture.cpp::TextureContentKey/find_content_texture/
    // cache_content_texture. Keep exact source bytes in the key so equality
    // cannot alias distinct guest textures after a hash collision.
    struct ContentKey {
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::uint8_t format = 0;
        std::uint8_t levels = 0;
        std::uint8_t generated_mips = 0;
        std::uint8_t tlut_format = 0;
        std::vector<std::uint8_t> source;
        std::vector<std::uint8_t> palette;

        bool operator==(const ContentKey&) const = default;
    };
    struct ContentKeyHasher {
        [[nodiscard]] std::size_t operator()(const ContentKey& key) const;
    };
    struct ContentEntry {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        std::uint64_t bytes = 0;
        std::list<ContentKey>::iterator lru;
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
    void release_upload_arenas();
    struct UploadAllocation {
        ID3D12Resource* resource = nullptr;
        std::uint8_t* cpu_base = nullptr;
        UINT64 offset = 0;
    };
    struct UploadArenaChunk {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint8_t* cpu_base = nullptr;
        UINT64 size = 0;
    };
    struct UploadArenaFrame {
        std::vector<UploadArenaChunk> chunks;
        std::size_t active_chunk = 0;
        UINT64 cursor = 0;
    };
    void append_upload_arena_chunk(UploadArenaFrame& frame, UINT64 min_size);
    [[nodiscard]] UploadAllocation allocate_upload_bytes(
        UINT64 size,
        UINT64 alignment);
    [[nodiscard]] std::uint32_t allocate_srv_index();

    std::unordered_map<Key, Entry, KeyHasher> entries_;
    detail::ConservativeGuestRangeEnvelope decoded_guest_envelope_;
    std::unordered_map<ContentKey, ContentEntry, ContentKeyHasher>
        content_entries_;
    std::list<ContentKey> content_lru_;
    std::uint64_t content_bytes_ = 0;
    std::uint64_t content_lookups_ = 0;
    std::uint64_t content_hits_ = 0;
    std::uint64_t content_decoded_misses_ = 0;
    std::uint64_t content_retained_bytes_ = 0;
    std::uint64_t content_evicted_bytes_ = 0;
    std::uint64_t content_evicted_entries_ = 0;
    // Full GX texture descriptor -> EFB-copy alias (checked before decode).
    std::unordered_map<EfbAliasKey, Entry, EfbAliasKeyHasher> efb_aliases_;
    detail::ConservativeGuestRangeEnvelope alias_guest_envelope_;
    std::vector<std::vector<Entry>> retired_entries_;
    std::vector<UploadArenaFrame> upload_arenas_;
    // Render-thread scratch storage for first-use texture decode/upload.
    // Keeping capacity between misses avoids heap churn during gameplay while
    // preserving exact decode bytes and GPU upload semantics.
    std::vector<std::vector<RGBA8>> level_pixels_scratch_;
    std::vector<UINT64> level_offsets_scratch_;
    std::vector<UINT> level_pitches_scratch_;
    unsigned current_frame_slot_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    UINT srv_stride_ = 0u;
    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_{};
    ID3D12GraphicsCommandList* upload_list_ = nullptr;
    std::uint32_t next_srv_index_ = 0;
    std::vector<std::uint32_t> free_srv_indices_;
    // Heap size and retirement bitmap must describe the same index domain.
    static constexpr std::uint32_t kSrvHeapCapacity = 4096u;
    std::bitset<kSrvHeapCapacity> retired_srv_indices_;
    // Kept monotonic across shutdown/reinitialization, like resource lifetimes.
    std::uint64_t retirement_revision_ = 0;

    // Absolute TMEM address domain, including every encoded TLUT slot and
    // maximum transfer/palette extent. Entries remain raw big endian.
    static constexpr std::size_t kTlutBankBytes = kTmemBytes;
    std::uint8_t tlut_bank_[kTlutBankBytes]{};
};

}  // namespace galaxy::gx
