#pragma once

// TextureStore: textures by id. A service copies an image's levels into mapped staging memory on the calling thread
// and queues the work; the render thread applies the queue at the first walk of the list submitted after it.
// Replacements (texture packs, the game's larger paintings, movies) take an id's place in `handle`.

#include "galaxy/windows_api.h"

#include "galaxy/render/device_gate.h"
#include "galaxy/render/gpu_texture_backend.h"
#include "galaxy/render/movie_playback.h"
#include "galaxy/render/texture_pack.h"
#include "galaxy/render/texture_sampler.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <nebula_cooked_texture.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct NebulaTexture;
struct NebulaTextureImage;

namespace galaxy::render {

struct RenderConfig;

struct RGBA8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

namespace detail {
// One level of a record (nebula_cooked_texture.h: its texels from `texels`, `width` x `height`) as RGBA8.
void expand_record_level(std::uint8_t layout, const std::uint8_t* texels, std::uint32_t width, std::uint32_t height,
                         RGBA8* out);
}  // namespace detail

// What a draw needs to bind one texture map.
struct TextureHandle {
    gpu::TextureObject* resource = nullptr;
    std::uint32_t srv_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_levels = 1;
    // Levels below the first were made by the cook: only the enhanced-mipmaps option samples them.
    bool generated_mips = false;
    // A replacement: width/height stay the game texture's, the resource is larger by 2^(lod_shift / 16) and
    // samplers shift their LOD range by that much.
    float lod_shift = 0.0f;
    bool lod_arbitrary = false;  // its levels pair with the game's by index (make_sampler_key)
};

class TextureStore {
public:
    static constexpr std::uint32_t kTargetBit = 0x80000000u;
    [[nodiscard]] static constexpr bool is_target(std::uint32_t id) noexcept { return (id & kTargetBit) != 0u; }
    // Whether a sampler reads the cook's generated levels of `handle`.
    [[nodiscard]] static bool samples_generated(const TextureSampler& mode, const TextureHandle& handle);
    // Whether anything can read the cook's generated levels with these settings: the enhanced-mipmaps option's samplers
    // (samples_generated) or ray tracing's hits, which pick a level by their footprint. Without either a draw's sampler keeps
    // to the first level (draw_walker), and only that level is staged.
    [[nodiscard]] static bool wants_generated_levels(const RenderConfig& render) noexcept;
    // Render thread, each frame after begin_frame: on a change of wants_generated_levels, the textures with generated levels
    // are made again with them or without them (the old texture draws until then).
    void follow_settings(const RenderConfig& render);

    struct Ops;

    bool initialize(gpu::Device* device, DeviceGate& gate);
    void shutdown();
    // After shutdown, on a new device with the gate closed: every live image queued again under its id.
    void restart(gpu::Device* device);

    // Any thread. The image's bytes are read before these return.
    [[nodiscard]] std::uint32_t register_image(const NebulaTextureImage& image);
    void update_image(std::uint32_t id, const void* bytes, std::uint32_t size);
    void free_image(std::uint32_t id);
    [[nodiscard]] std::uint32_t new_target();
    // `record`, the game's larger painting of texture `id`, shown unless a pack names it; null: that one goes.
    void set_built_in(std::uint32_t id, const void* record);
    // What was queued since the last call: the render thread applies it when the list after it first walks.
    [[nodiscard]] std::shared_ptr<Ops> take_ops();

    // Render thread.
    void begin_frame(unsigned frame_slot, unsigned frames_in_flight);
    // Applies `ops` through this frame's upload list, after release_held. Its frees are held while the list they came
    // with walks.
    void apply_ops(Ops& ops, std::vector<std::uint32_t>& freed);
    // At each list's first walk: the frees the last list came with go, and their ids, targets too, join `freed`.
    void release_held(std::vector<std::uint32_t>& freed);
    [[nodiscard]] TextureHandle handle(std::uint32_t id);
    // Bumps whenever `handle` of a live id changes.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // A list that keeps its texture handles from walk to walk (Vulkan's bound tables) names texture `id` again by this.
    void touch(std::uint32_t id) noexcept {
        if (id < entries_.size() && entries_[id] != nullptr) {
            entries_[id]->named = frame_;
            backend_.note_named(entries_[id]->current.resource);
        }
    }
    // At a frame boundary, with the adapter's video memory `usage` and its `budget` (0: not known): past the budget, the textures
    // that no frame in flight names give back their top levels, the least recently named first, the largest first among equals,
    // at most kReduceBytesPerBatch freed a batch and one batch every kReduceBatchFrames. A texture is never blank: the old one
    // draws until the smaller one (made from the game's image with its top levels left out, by the next take_ops) takes its place,
    // and a texture a walk names again while there is room is made whole again the same way. Memory comes back as the frames that
    // may read the old texture finish (and, on Direct3D 12, when a whole heap empties). Render thread.
    void enforce_budget(std::uint64_t usage, std::uint64_t budget);
    static constexpr std::uint64_t kReduceBytesPerBatch = 64ull << 20u;
    static constexpr std::uint64_t kRestoreBytesPerFrame = 32ull << 20u;
    static constexpr std::uint64_t kReduceBatchFrames = 12u;
    struct BudgetStats {
        std::uint32_t resident = 0, reduced = 0;  // ids with a texture, ids whose top levels are left out now
        std::uint64_t resident_bytes = 0, reduced_bytes = 0;  // what the textures hold; what the reduced ones leave out
        std::uint64_t reductions = 0, freed_bytes = 0, restores = 0;  // since the start
    };
    [[nodiscard]] BudgetStats budget_stats() const;
    // The bytes of the staging chunks that exist now (in use, kept for frames in flight, or spare), any thread.
    [[nodiscard]] static std::uint64_t staging_bytes() noexcept;

    // A view slot for a picture the renderer owns; retire_view returns both after the frame's fence.
    [[nodiscard]] std::uint32_t allocate_view() { return allocate_srv_index(); }
    void retire_view(std::uint32_t index, gpu::TextureRef picture) { retire(std::move(picture), index); }

    [[nodiscard]] const TextureBackend& backend() const noexcept { return backend_; }
    [[nodiscard]] TextureBackend& backend() noexcept { return backend_; }

    // Texture packs (texture_pack.h). apply_texture_packs runs once per frame after the upload list is set: uploads
    // finished replacements and swaps them in; a new index or a changed dump mode looks every texture up again.
    void configure_texture_packs(const TexturePackSettings& settings);
    void apply_texture_packs();
    [[nodiscard]] TexturePacks::Stats texture_pack_stats() const noexcept { return packs_.stats(); }

    // The movie: `frame`'s planes as textures NEBULA_TEXTURE_MOVIE_Y, _U and _V until the next call; null: none.
    void set_movie_override(const MovieFrameMark& mark, const MovieFrame* frame);
    [[nodiscard]] bool movie_override_active() const noexcept { return movie_active_; }

private:
    using ChunkRef = std::shared_ptr<TextureBackend::UploadChunk>;
    struct Staged {
        ChunkRef chunk;
        std::uint64_t offset = 0u;
        std::uint32_t width = 0u, height = 0u;
        std::uint32_t levels = 0u;
        TexelFormat format = TexelFormat::Rgba8;
        std::uint8_t layout = 0u;
        std::array<std::uint64_t, NEBULA_TEXTURE_MAX_LEVELS> offsets{};
        std::array<std::uint32_t, NEBULA_TEXTURE_MAX_LEVELS> pitches{};
    };
    // A texture's name in the game (its record's name_hash; texture packs and dumps go by it) and source format. Images
    // the game makes (render targets, generated art) have none.
    struct PackName {
        std::uint64_t hash = 0u;
        std::uint8_t format = 0u;
        bool named = false;
    };
    struct Op {
        enum class Kind : std::uint8_t { Create, Update, Free, FreeTarget, BuiltIn };
        Kind kind = Kind::Create;
        std::uint32_t id = 0u;
        Staged staged;
        PackName name;
        bool generated = false;
        // `trimmed`: a record's first level alone, its generated levels left out (nothing reads them: wants_generated_levels).
        // `full_levels`: the levels the image has.
        bool trimmed = false;
        std::uint32_t full_levels = 1u;
        // A Create with `resize` makes an existing id's texture again with its `drop` top levels left out (0: whole). It is
        // dropped if the id is gone or already has that many dropped and is trimmed as it is.
        bool resize = false;
        std::uint32_t drop = 0u;
    };
    struct Entry {
        gpu::TextureRef texture;
        TextureHandle own;
        TextureHandle current;
        // What stands in for it, with its own view.
        gpu::TextureRef shown;
        PackName name;
        std::uint32_t pack_slot = TexturePacks::kNone;
        bool pack_waiting = false;
        TextureReplacement built_in;
        // The frame a walk last named it, the bytes its own texture holds, and what the budget made of it: the top levels left
        // out of its own texture (`dropped`; own.width and own.height stay the game's), the levels and bytes it has whole.
        std::uint64_t named = 0u;
        std::uint64_t bytes = 0u;
        std::uint64_t full_bytes = 0u;
        std::uint64_t asked_at = ~std::uint64_t{0};  // the frame a resize of it was last asked for
        std::uint64_t demanded_at = ~std::uint64_t{0};  // the frame a walk last wanted it whole with no room for it
        std::uint32_t dropped = 0u;
        std::uint32_t full_levels = 0u;
        bool trimmed = false;  // its generated levels left out (Op::trimmed)
    };
    struct ResizeRequest {
        std::uint32_t id = 0u;
        std::uint32_t drop = 0u;
    };
    struct Retired {
        gpu::TextureRef texture;
        std::uint32_t view = ~0u;
        std::uint64_t evicted_bytes = 0u;  // what a budget resize freed: counted in evicting_bytes_ until it is released
    };
    struct LinearImage {
        std::uint32_t width = 0u, height = 0u;
        std::uint8_t format = 0u;
    };
    // The game's memory, valid until the image is freed.
    struct Source {
        const void* bytes = nullptr;
        std::uint32_t size = 0u;
        std::uint32_t width = 0u, height = 0u;
        std::uint8_t format = 0u;
    };

    [[nodiscard]] Staged stage(const std::uint8_t* texels, std::uint32_t width, std::uint32_t height,
                               std::uint32_t levels, std::uint8_t layout);
    [[nodiscard]] ChunkRef allocate_staging(std::uint64_t bytes, std::uint64_t& offset);
    // `drop` leaves that many top levels out of a record (false when it has no more levels than that, or is a single image).
    bool stage_image(Op& op, const void* bytes, std::uint32_t size, std::uint32_t width, std::uint32_t height,
                     std::uint8_t format, std::uint32_t drop = 0u);
    void queue_built_in(std::uint32_t id, const void* record);
    void create_entry(Op& op);
    // `once`: the texture's levels are written once (a new id), so it may be a direct-write texture; an id that is updated
    // keeps a texture the GPU copies into.
    void make_own(Entry& entry, const Op& op, bool once);
    void update_entry(Op& op);
    void resize_entry(Op& op);
    // An entry whose generated levels are not as generated_levels_ says (staged while that changed): made again.
    void follow_generated_levels(std::uint32_t id, Entry& entry);
    void free_entry(std::uint32_t id);
    void apply_built_in(Op& op);
    void dump_base_level(const Staged& staged, const PackName& name);
    void keep(ChunkRef chunk);
    void look_up(std::uint32_t id, Entry& entry);
    void show(Entry& entry, const TextureReplacement& replacement, std::uint32_t slot);
    void stop_showing(Entry& entry);
    void retire(gpu::TextureRef texture, std::uint32_t view, std::uint64_t evicted_bytes = 0u);
    // Asks the next take_ops for texture `id` made again with `drop` top levels left out.
    void request_resize(std::uint32_t id, Entry& entry, std::uint32_t drop);
    [[nodiscard]] std::uint32_t allocate_srv_index();

    // Calling threads, under lock_.
    std::mutex lock_;
    std::shared_ptr<Ops> queued_;
    std::uint32_t next_image_ = 1u, next_target_ = 1u;
    std::unordered_map<std::uint32_t, LinearImage> linear_images_;
    std::unordered_map<std::uint32_t, Source> sources_;
    std::unordered_map<std::uint32_t, const void*> built_ins_;
    std::vector<ResizeRequest> resize_requests_;  // the budget's: staged by the next take_ops
    // wants_generated_levels as the render thread last saw it, for the calling threads' staging.
    std::atomic<bool> generated_levels_wanted_{false};
    DeviceGate* gate_ = nullptr;
    ChunkRef arena_chunk_;
    std::uint64_t arena_used_ = 0u;
    std::vector<ChunkRef> free_chunks_;
    // A request for staging came since the last begin_frame, and the frames since one did (the spare chunks go when none for long).
    bool staging_requested_ = false;
    unsigned staging_idle_frames_ = 0u;

    // Render thread. Ids are never used again, so a table naming a freed image reads nothing, not another image.
    std::vector<std::unique_ptr<Entry>> entries_;
    std::uint64_t revision_ = 1u;
    bool generated_levels_ = false;  // wants_generated_levels, as generated_levels_wanted_ was last set
    // Budget: frames begun, bytes of replaced textures still held by frames in flight, the bytes a walk may still ask to have
    // back this frame, the frame the next batch of reductions may start, and the counts since the start. (Render thread.)
    std::uint64_t frame_ = 0u;
    std::uint64_t evicting_bytes_ = 0u;
    std::uint64_t restore_room_ = 0u;
    std::uint64_t restore_demand_ = 0u;  // bytes walks wanted whole this frame with no room: the next enforce_budget makes room
    std::uint64_t reduce_after_frame_ = 0u;
    std::uint64_t reductions_ = 0u, freed_bytes_total_ = 0u, restores_ = 0u;
    std::unordered_set<std::uint32_t> blank_images_;
    // The frees the list walking now came with.
    std::vector<std::uint32_t> held_frees_;
    std::vector<std::vector<Retired>> retired_;
    std::vector<std::vector<ChunkRef>> kept_;
    unsigned frame_slot_ = 0u;

    TexturePacks packs_;
    bool stager_set_ = false;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> pack_waiters_;
    std::vector<std::pair<std::uint32_t, TexturePackPixels>> pack_uploads_;
    std::vector<std::vector<TextureBackend::UploadChunk>> pack_staging_;
    std::uint64_t pack_named_ = 0u, pack_bound_ = 0u, pack_swapped_ = 0u, pack_stats_frame_ = 0u;
    std::uint64_t pack_apply_ns_ = 0u, pack_apply_max_ns_ = 0u;

    struct MoviePlane {
        gpu::TextureRef texture;
        std::uint32_t srv_index = ~0u;
        std::uint32_t width = 0u, height = 0u;
        bool uploaded = false;
    };
    static constexpr unsigned kMovieRing = 4u;  // frames a plane's texture rests before it is written again
    std::array<std::array<MoviePlane, 3>, kMovieRing> movie_ring_{};
    unsigned movie_ring_at_ = 0u;
    bool movie_active_ = false;
    std::int32_t movie_frame_ = -1;
    const void* movie_staging_seen_ = nullptr;
    std::array<std::uint32_t, 3> movie_ids_{};
    std::array<TextureHandle, 3> movie_handles_{};
    std::vector<std::vector<std::shared_ptr<TextureBackend::UploadChunk>>> movie_staging_;

    TextureBackend backend_;
    static constexpr std::uint32_t kViewPageSize = 65536u;
    std::uint32_t next_srv_index_ = 0u;
    std::vector<std::uint32_t> free_srv_indices_;
};

}  // namespace galaxy::render
