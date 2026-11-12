// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The renderer's side of the frame list: what the game registered, how two lists pair, and the one walk every presented
// frame takes. Real frames walk at t = 1, generated frames at their t.

#include "galaxy/render/device_gate.h"
#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/frame_interpolation.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/texture_sampler.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/renderer_common.h"
#include "render/core/compose.h"
#include "render/core/gpu_encoder.h"

#include <nebula_frame_list.h>
#include <nebula_game_api.h>
#include <nebula_model.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace galaxy::render {
struct TextureHandle;
}

namespace galaxy::render::core {

// A light set in the rows: one header row (x the light count, as a float; program.hlsli light_count), then four rows a light.
// The rows before the sets hold an empty set, the one a draw without lights reads.
inline constexpr std::uint32_t kLightSetHeaderRows = 1u;
inline constexpr std::uint32_t kRowsPerLight = 4u;
[[nodiscard]] constexpr std::uint32_t light_set_rows(std::uint32_t lights) noexcept {
    return kLightSetHeaderRows + kRowsPerLight * lights;
}
static_assert(sizeof(NebulaFrameLight) == kRowsPerLight * 16u);
// An instanced particle's rows (NebulaParticleInstance), as particle_instances.hlsl reads them.
inline constexpr std::uint32_t kParticleRows = 10u;
static_assert(sizeof(NebulaParticleInstance) == kParticleRows * 16u);
// An instanced glyph's rows (NebulaGlyphInstance), as glyph_instances.hlsl reads them.
inline constexpr std::uint32_t kGlyphRows = 3u;
static_assert(sizeof(NebulaGlyphInstance) == kGlyphRows * 16u);

// A presented frame's place between two lists.
struct WalkTime {
    float t = 1.0f;
    float t_prev = 0.0f;      // of the frame presented before it, in this list's pair
    bool generated = false;   // a frame between lists
    bool reset = false;       // motion history starts again
};

// A mesh on the GPU. Model meshes have the four streams, site meshes one.
struct GpuMesh {
    gpu::Address positions = 0u;
    gpu::Address attributes = 0u;
    gpu::Address matrices = 0u;
    gpu::Address further = 0u;
    gpu::Address indices = 0u;
    gpu::Address refined_indices = 0u;
    std::uint32_t vertex_count = 0u, refined_vertex_count = 0u;
    std::uint32_t index_count = 0u, refined_index_count = 0u;
    std::uint32_t site_stride = 0u;     // nonzero: one stream at `positions`
    std::uint32_t further_stride = 0u;
    std::uint32_t single_matrix = NEBULA_MODEL_NONE;
    std::uint8_t index_bytes = 4u;
    std::uint8_t billboard = 0u;  // NebulaModelMesh.billboard: 1 a billboard, 2 a Y billboard (it turns to the eye)
    // What a ray reads of the mesh's material (nebula_model.h NebulaModelHit): its class and flags, what it shows with
    // no light, its colour under full light, and its numbers.
    std::uint8_t hit_class = 0u, hit_flags = 0u;
    float hit_emission[3]{}, hit_base[3]{0.5f, 0.5f, 0.5f};
    float hit_roughness = 0.45f, hit_metal = 0.0f, hit_transmission = 0.0f;
    float hit_environment = 0.0f;  // how much of its look a painted reflection carries
    float glow[4]{};  // the light it gives: colour, and its strength (0: none)
    std::uint8_t glow_source = 0u;  // NebulaModelHit.glow_source: where the glow's colour comes from as it animates
    std::uint8_t hit_env_map = 0xFFu;
    float hit_ior = 1.5f, hit_thickness = 0.0f;
    float hit_env_tint[3]{};
    float hit_body = -1.0f, hit_shine = -1.0f, hit_tint_keep = -1.0f;  // the review's; below 0: the class's own
    // The live colour of glow_source (L-1): its row among the material's form 0 rows (vertex rows first; 0xFFFF
    // none), how many rows that form has, and the row's cooked colour, which the live one is measured against.
    std::uint16_t glow_row = 0xFFFFu;
    std::uint16_t glow_row_count = 0u;
    float glow_cooked[3]{};
    // Map 0's coordinate (plan 13.3, the water's ripple): the first of its two rows (u, v) among the material's own
    // form's rows (vertex rows first; 0xFFFF none: no stage samples map 0 through a coordinate made from a uv set by a
    // matrix), the uv set it reads, and that form's row count (a draw's rows are the form's where they agree).
    std::uint16_t map0_row = 0xFFFFu;
    std::uint16_t map0_form_rows = 0u;
    std::uint8_t map0_uv = 0u;
    float hit_alpha = 0.5f;  // an alpha-tested material: the alpha from which it is there
    // Its paint as one light (nebula_model.h NebulaModelPaintLight; the Physical style, plan 14.6): the share b / (a + b)
    // (0: it stays painted) and the light's direction in the model's space, octahedral.
    float paint_share = 0.0f;
    std::int16_t paint_direction[2]{};
    float min[3]{}, max[3]{};
    std::uint32_t object_slots_first = 0u, object_slots_count = 0u;
    std::uint32_t arena = 0u;
    std::uint32_t model = 0u;
    // A mesh registered on its own (mesh_register): its place in `arena`, given back when it retires. Zero for a model's
    // meshes, whose place is the model's.
    std::size_t arena_offset = 0u, arena_size = 0u;
};

struct GpuModel {
    std::vector<NebulaModelDrawMatrix> draw_matrices;
    std::vector<NebulaModelEnvelope> envelopes;
    std::vector<NebulaModelMix> mixes;
    std::vector<float> inverse_binds;  // 12 per joint
    std::vector<NebulaModelJoint> joints;
    std::vector<ao::ObjectSlot> object_slots;
    ModelTables tables{};
    std::uint32_t first_mesh = 0u, mesh_count = 0u;
    std::uint32_t arena = 0u;
    std::uint32_t generation = 0u;
    // The model's geometry in `arena`: where, and how many bytes (zero: none placed), given back when the model retires.
    std::size_t arena_offset = 0u, arena_size = 0u;
};

// One registered list of textures: what it names, and what the draws read it through (this walk's texture list of
// {texture slot, sampler slot} pairs; Vulkan without descriptor indexing: the bound tables).
struct TextureTable {
    std::vector<NebulaFrameTexture> list;
    std::uint32_t count = 0u;
    std::vector<std::uint32_t> sampler_slots;
    gpu::Address lists = 0u;
    std::uint32_t list_first = 0u;
    bool clamp_logged = false;  // a bound table drew fewer maps than the list names (told once)
    std::uint64_t key = 0u;
    gpu::Table textures{};
    gpu::Table samplers{};
    std::uint64_t textures_revision = ~0ull;
    std::uint32_t samplers_generation = 0u;
    float samplers_mip_lines = 0.0f, samplers_scene_bias = 0.0f;
    bool has_target = false;
    std::uint64_t walk_stamp = 0u;
    // The last frame (RenderCore::frame_index_) a walk named the table; a table idle for kTableIdleFrames gives up what was built
    // for it (retire_idle_tables) and is `retired` until a walk names it again.
    std::uint64_t last_frame = 0u;
    bool retired = false;
    std::uint64_t touched_walk = 0u;  // the walk (RenderCore::walk_.serial) that last named its textures to the store (bound tables)
};

struct StaticSlot {
    gpu::Address vertex_rows = 0u, pixel_rows = 0u;
    // A registered material's rows as registered (vertex rows, then pixel rows): the registries' copy, kept for their
    // life. Null for a frame's own constants (their rows are the list's).
    const float* cpu = nullptr;
};

struct PipelineEntry {
    NebulaPipelineDesc desc{};
    std::vector<NebulaLayoutInput> inputs;  // desc.input_count of them
    bool reads_world = false;  // its vertex program reads the world rows
    bool lit_form = false;     // its pixel program lights at the pixel
    // Where its vertices keep their matrix number (NEBULA_INPUT_MATRIX, one uint, stream 0), or none.
    std::uint32_t matrix_at = NEBULA_NONE32;
};

// The view constant (b0, program.hlsli).
struct ViewRows {
    float projection[4][4];
    float previous[2][4];
    float motion[4];
    float raster[4];
    float sky_up[3];          // the player's up in eye space (unit), else the view's up
    std::uint32_t view_flags; // bit 0 sky up known, bit 1 enhanced, bit 2 wide target, bit 3 traced light (`traced`), bit 4 shown
    float scale[4];
    float traced[4];  // with view_flags bit 3: the light image's place among the textures, the added light's, the mirror's,
                      // the facing picture's
    float stage_a[4]; // with view_flags bit 3: the stage's ambient colour A (rgb), w kShadowTint (program.hlsli)
    float stage_k[4]; // with view_flags bit 3: the stage's key colour K (rgb)
    float volume[3][4];  // with view_flags bit 3, the scene's view (FB, the light volume: program.hlsli traced_air): the
                         // depth's eye terms (rt_eye_z), the viewport it spans, its picture's and sampler's places and 1
    float transmit[4];  // with view_flags bit 3, the scene's view: x picture 6's place (T, rt_pass_transmit), y 1
                        // where the pass drew this walk
    float blend_guide[4];  // with view_flags bit 3, the scene's view (FB, 15.2: program.hlsli traced_by_air,
                           // traced_mirror_own): x the guide blended draws compare with, y 1 the second layer's
                           // (picture 9), 2 the opaque layer's (picture 8), 0 none
    float look[4];  // with view_flags bit 3: the painted composite's look (program.hlsli traced_painted; RenderCore
                    // rt_look_): kForm, the sun's height over the player's up (at least 0.5), kDebake, kAmbientLook
    float dev[2][4];  // with view_flags bit 3: the programs' development tunables (rt_dev.h from BaseFloor): [0] the
                      // values, [1] 1 where the run gave one
};
// The cbuffer's packing: view_sky_up and view_flags share one 16-byte row.
static_assert(offsetof(ViewRows, sky_up) == 128 && offsetof(ViewRows, view_flags) == 140 &&
              offsetof(ViewRows, scale) == 144 && offsetof(ViewRows, traced) == 160 && offsetof(ViewRows, stage_a) == 176 && offsetof(ViewRows, stage_k) == 192 &&
              offsetof(ViewRows, volume) == 208 &&
              offsetof(ViewRows, transmit) == 256 && offsetof(ViewRows, blend_guide) == 272 && offsetof(ViewRows, look) == 288 &&
              offsetof(ViewRows, dev) == 304 && sizeof(ViewRows) == 336);

// Everything one list needs that lasts as long as the list.
struct ListData {
    NebulaFrameList* list = nullptr;
    std::uint64_t fence = 0u;
    std::uint32_t serial = 0u;

    // The pair with the list before; made on first need.
    bool paired = false;
    bool cut = false;
    std::vector<std::uint32_t> block_before;
    std::vector<std::uint32_t> camera_before;
    std::vector<CameraMotion> camera_motion;
    std::vector<std::uint32_t> motion_first;   // per block: its first in `motions`, or none
    std::vector<AffineMotion> motions;
    std::vector<std::uint32_t> joint_motion;   // per block's joint in order, index in `motions` or none
    std::vector<std::uint32_t> constants_before;
    std::vector<std::uint32_t> view_before;
    std::vector<std::uint32_t> light_set_before;
    std::vector<std::uint32_t> item_before;
    bool motion_ready = false;
    // The list's screen probes are asked once, on its first real walk: a frame between lists (`between`) draws the scene at
    // t < 1, not the camera the game's points were made with.
    bool probes_done = false;
    [[nodiscard]] bool probes_due(bool between) const noexcept { return !probes_done && !between; }
    std::vector<EyeMotion> eye_motions;        // an eye-space block's joints, in `joint_motion`'s place

    // Target passes drawn before the list's other passes (plan_early_targets): passes [first, end), and the
    // SCENE_BEGIN they sit after in the list, or none.
    struct EarlyTargets {
        std::uint32_t first = 0u, end = 0u, scene = NEBULA_NONE32;
    };
    bool targets_planned = false;
    std::vector<EarlyTargets> early_targets;
    // Probes asked right after an earlier pass (plan_early_probes), in list order; `after` a SCENE_END: asked as the
    // scene ends, before walk_scene_end trades the targets.
    struct EarlyProbe {
        std::uint32_t after = 0u, probe = 0u;
    };
    std::vector<EarlyProbe> early_probes;
    // Clears of the scene's depth that nothing after them in the list reads or writes (plan_late_depth_clears).
    std::vector<std::uint32_t> late_depth_clears;

    // Read once from the list.
    bool scanned = false;
    std::vector<std::uint32_t> block_camera;   // the camera an eye-space block holds still under, or none
    std::vector<std::uint32_t> light_camera;
    std::vector<std::uint8_t> block_world_wanted;

    // The last walk's: where the rows and vertices were, and the view terms.
    std::vector<std::uint32_t> block_rows;
    std::vector<std::array<float, 6>> view_terms;
    gpu::Address ring_address = 0u;
    std::uint64_t rows_walk = 0u;
};

struct RegistryOps {
    std::vector<std::pair<std::uint32_t, GpuModel>> models;
    std::vector<std::pair<std::uint32_t, GpuMesh>> meshes;
    std::vector<StaticSlot> slots;
    std::vector<PipelineEntry> pipelines;
    std::vector<TextureTable> tables;
    std::vector<std::uint32_t> freed_models, freed_meshes;
};

class Registries {
public:
    void initialize(gpu::Device* device, DeviceGate& gate);
    void shutdown() noexcept;
    // A lost card: its memory goes, then everything live is queued again under its id, read from the game's files.
    void lose_device() noexcept;
    void restart(gpu::Device* device);

    // Any thread.
    std::uint32_t model_register(const void* file, std::uint32_t* first_mesh);
    void model_free(std::uint32_t model);
    std::uint32_t mesh_register(const NebulaFrameMesh& mesh);
    void mesh_free(std::uint32_t mesh);
    std::uint32_t material_register(const float (*rows)[4], std::uint32_t vertex_rows, std::uint32_t pixel_rows);
    std::uint32_t pipeline_register(const NebulaPipelineDesc& desc, const NebulaLayoutInput* inputs);
    std::uint32_t texture_table(const NebulaFrameTexture* list, std::uint32_t count);

    // Game thread, as a list is handed over: what the services queued since the last list.
    [[nodiscard]] RegistryOps take_ops();
    // Render thread, at that list's first walk. Its frees are held until the next list's ops arrive (it walks, maybe
    // more than once, with them named), then wait for `submitted_fence` of that call to complete (reclaim).
    void apply(RegistryOps& ops, std::uint64_t submitted_fence);
    void reclaim(std::uint64_t completed_fence);
    // The meshes apply has retired since the last call (render thread): no list names them again, and their vertices
    // go once the GPU is past the work submitted so far, so nothing recorded from here on may read them.
    // Kept only while `tell_retired_meshes(true)`.
    [[nodiscard]] std::vector<std::uint32_t> take_retired_meshes() noexcept { return std::exchange(retired_meshes_, {}); }
    void tell_retired_meshes(bool on) noexcept {
        tell_retired_meshes_ = on;
        retired_meshes_.clear();
    }
    // Tables naming a freed texture go; their descriptor tables join `retired`.
    void forget_textures(std::span<const std::uint32_t> freed, std::vector<gpu::Table>& retired);
    // Render thread, every kTableSweepFrames: the one rule for a table's life. A table no walk has named for kTableIdleFrames
    // frames releases what the renderer built for it (a Vulkan device without descriptor indexing: its persistent texture table,
    // which joins `retired` for the renderer to hand back once the fence of the work that may read it has passed; a device with
    // texture lists builds nothing per table) and the arrays it caches per walk; the registered list itself stays, and a
    // walk that names the table again builds it anew. A table's id is kept for the session: the game keeps the ids it was
    // given in its materials (checked against its own stamps), so an id given to another list would draw the wrong textures.
    static constexpr std::uint64_t kTableIdleFrames = 600u;
    static constexpr std::uint64_t kTableSweepFrames = 60u;
    // `idle_frames`: the idle time in frames as the renderer counts them (kTableIdleFrames at 60 a second, more when each game
    // frame is shown in several).
    void retire_idle_tables(std::uint64_t frame, std::vector<gpu::Table>& retired, std::uint64_t idle_frames = kTableIdleFrames);
    struct TableCounts {
        std::uint32_t live = 0u;     // named within kTableIdleFrames
        std::uint32_t retired = 0u;  // idle longer: built resources released
        std::uint64_t retirements = 0u;  // retire_idle_tables's releases since the start
    };
    [[nodiscard]] TableCounts table_counts() const noexcept;

    [[nodiscard]] const GpuMesh& mesh(std::uint32_t id) const noexcept { return meshes_[id]; }
    [[nodiscard]] const GpuModel& model(std::uint32_t id) const noexcept { return models_[id]; }
    [[nodiscard]] const StaticSlot& slot(std::uint32_t id) const noexcept { return slots_[id]; }
    [[nodiscard]] const PipelineEntry& pipeline(std::uint32_t id) const noexcept { return pipelines_[id]; }
    void write_pipeline_list(const std::filesystem::path& path);
    [[nodiscard]] std::uint32_t pipeline_count() const noexcept { return static_cast<std::uint32_t>(pipelines_.size()); }
    // Every pipeline the game has registered (any thread), those the render thread has not been handed yet among them.
    [[nodiscard]] std::uint32_t registered_pipeline_count() const noexcept {
        return registered_pipelines_.load(std::memory_order_acquire);
    }
    [[nodiscard]] TextureTable& table(std::uint32_t id) noexcept {
        return id < tables_.size() && tables_[id] != nullptr ? *tables_[id] : no_table_;
    }
    [[nodiscard]] std::span<const ao::ObjectSlot> object_slots(const GpuMesh& mesh) const noexcept;
    [[nodiscard]] std::uint64_t memory_bytes() noexcept;
    // What the geometry pages and the material slot pages hold (any thread).
    struct MemoryStats {
        std::uint32_t pages = 0u;         // geometry pages holding memory
        std::uint32_t empty_pages = 0u;   // of them, with no model or mesh in
        std::uint64_t page_bytes = 0u;    // their memory
        std::uint64_t local_bytes = 0u;   // of it, in video memory
        std::uint64_t live_bytes = 0u;    // the part of it placed models and meshes hold
        std::uint64_t slot_bytes = 0u;    // material slot pages (never given back: a slot's id is the game's)
        std::uint32_t slots = 0u;         // material slots registered
    };
    [[nodiscard]] MemoryStats memory_stats() noexcept;
    // Arena `index`'s memory, where the meshes placed in it lie (GpuMesh::arena): its first byte and its size. False
    // when there is no such arena now.
    [[nodiscard]] bool arena_memory(std::uint32_t index, gpu::Address& base, std::size_t& bytes) noexcept;

private:
    // A page of upload memory models and meshes are placed in: free ranges (offset -> size), taken best fit and given back
    // when what was placed retires, so a model that lives on does not keep the space of the ones freed around it.
    struct Arena {
        gpu::UploadMemory memory;  // video memory where the device offers it: written in order, never read back
        std::size_t bytes = 0u;
        std::size_t free_bytes = 0u;
        std::map<std::size_t, std::size_t> free;
        std::uint32_t live = 0u;
    };
    struct Placed {
        std::byte* cpu = nullptr;
        gpu::Address gpu = 0u;
        std::uint32_t arena = 0u;
        std::size_t offset = 0u, size = 0u;
    };
    struct Retired {
        std::uint64_t fence = 0u;
        std::uint32_t arena = 0u;
        std::uint32_t model = 0u;
        std::uint32_t first_mesh = 0u, mesh_count = 0u;
        bool has_geometry = false;
        std::size_t arena_offset = 0u, arena_size = 0u;
    };
    struct SlotRecord {
        std::uint32_t vertex_rows = 0u, pixel_rows = 0u, id = 0u;
        std::unique_ptr<float[]> cpu;
    };
    struct TableKey {
        std::vector<NebulaFrameTexture> list;
        std::uint32_t count = 0u;
        std::uint32_t id = 0u;
    };
    struct LiveModel {
        const void* file = nullptr;  // valid until model_free
        std::uint32_t first_mesh = 0u;
    };
    struct LiveMesh {
        std::unique_ptr<std::byte[]> bytes;
        std::uint32_t vertex_bytes = 0u, stride = 0u, index_count = 0u;
    };
    Placed place(std::size_t bytes);
    // `bytes` from `source` into the placed range: one sequential write.
    static void fill(const Placed& placed, const void* source, std::size_t bytes) noexcept;
    // `size` bytes at `offset` of page `index` are free again (lock_ held). The last thing leaving a page frees the page's
    // memory, except for one spare page that stays for the next scene.
    void give_back(std::uint32_t index, std::size_t offset, std::size_t size);
    void place_model(std::uint32_t id, const LiveModel& live, GpuModel model, std::vector<GpuMesh>& meshes, const Placed& placed);
    void place_mesh(std::uint32_t id, const LiveMesh& live);
    void place_slot(const float (*rows)[4], std::uint32_t vertex_rows, std::uint32_t pixel_rows);
    std::uint32_t material_register_locked(const float (*rows)[4], std::uint32_t vertex_rows, std::uint32_t pixel_rows);
    std::uint32_t take_mesh_ids(std::uint32_t count);

    gpu::Device* device_ = nullptr;
    DeviceGate* gate_ = nullptr;
    std::mutex lock_;
    std::vector<Arena> arenas_;
    std::unordered_map<std::uint32_t, LiveModel> live_models_;
    std::unordered_map<std::uint32_t, LiveMesh> live_meshes_;
    std::vector<Retired> retired_;
    // The frees the list walking now came with (render thread).
    std::vector<std::uint32_t> held_freed_models_, held_freed_meshes_;
    std::vector<std::uint32_t> retired_meshes_;
    bool tell_retired_meshes_ = false;

    // Written under lock_ by services.
    RegistryOps incoming_;
    std::vector<std::uint32_t> free_models_;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> free_mesh_runs_;

    std::vector<GpuModel> models_;
    std::vector<GpuMesh> meshes_;
    std::vector<StaticSlot> slots_;
    std::vector<PipelineEntry> pipelines_;
    // A freed table's id stays empty: a stale id reads no table, not another one.
    std::vector<std::unique_ptr<TextureTable>> tables_;
    TextureTable no_table_;
    std::uint64_t table_retirements_ = 0u;

    std::uint32_t next_model_ = 1u, next_mesh_ = 1u, next_slot_ = 0u, next_table_ = 1u;
    // Every pipeline the game registered, with its inputs (the entries' copies, in registration order).
    std::vector<PipelineEntry> all_pipelines_;
    std::atomic<std::uint32_t> registered_pipelines_{0u};  // all_pipelines_'s size, read without the lock
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> pipeline_by_hash_;
    std::unordered_map<std::uint64_t, std::vector<TableKey>> table_by_hash_;
    std::unordered_map<std::uint64_t, std::vector<SlotRecord>> slot_by_hash_;

    gpu::UploadMemory slot_page_;
    std::size_t slot_page_used_ = 0u, slot_page_capacity_ = 0u;
    std::uint64_t slot_bytes_ = 0u;
    std::vector<gpu::UploadMemory> slot_pages_;
};

// Whether ring item `now` has the shape of its pair `before`: the same vertex count, index count and vertex stride. Only
// then are the pair's vertices blended and read as the frame before's; an item whose shape changed (a strip that grew, a
// polygon clipped to fewer corners) shows its newest vertices, as a teleport does.
inline bool ring_shape_matches(const NebulaFrameItem& now, const NebulaFrameItem& before) noexcept {
    return (before.flags & NEBULA_ITEM_RING) != 0u && before.ring_count == now.ring_count && before.count == now.count &&
           before.mesh == now.mesh;
}

// Pairing: Pairing::pair fills `now`'s before-tables from `before`. No GPU, no Windows: tested offline.
class Pairing {
public:
    void pair(ListData& now, const ListData* before);
    // The joints' motions, once per moving matrix.
    void prepare_motion(ListData& now, const ListData& before);
    std::uint64_t teleports = 0u, flips = 0u, camera_cuts = 0u, game_cuts = 0u;

private:
    struct SlotEntry {
        std::uint32_t list = 0u;
        std::uint32_t first = NEBULA_NONE32;
    };
    std::vector<SlotEntry> slots_;
    std::vector<std::uint32_t> next_in_slot_;
    std::vector<std::uint8_t> used_;
    std::vector<std::uint32_t> next_item_;
    // OTHER-role cameras pair by the nearest view matrix: every (now, before) candidate, closest first.
    struct CameraCandidate {
        float distance;
        std::uint32_t now, before;
    };
    static constexpr std::uint64_t kNearestCameraPairs = 65536u;
    std::vector<CameraCandidate> camera_pairs_;
    std::vector<std::uint8_t> camera_used_;
    float camera_move_ = 0.0f;
};

// A block's joints at t through its motions, `out` three rows each. An eye-space block's are eye matrices under `camera_t`.
void joints_at(const ListData& now, const NebulaFrameBlock& block, std::uint32_t block_index, float t, const Affine34* camera_t,
               float (*out)[4]) noexcept;

}  // namespace galaxy::render::core
