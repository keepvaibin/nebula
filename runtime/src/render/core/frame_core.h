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
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace galaxy::render {
struct TextureHandle;
}

namespace galaxy::render::core {

inline constexpr std::uint32_t kLightRows = 4u * NEBULA_LIGHTS_PER_SET;
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
    float min[3]{}, max[3]{};
    std::uint32_t object_slots_first = 0u, object_slots_count = 0u;
    std::uint32_t arena = 0u;
    std::uint32_t model = 0u;
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
};

struct StaticSlot {
    gpu::Address vertex_rows = 0u, pixel_rows = 0u;
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
    std::uint32_t view_flags; // bit 0 sky up known, bit 1 enhanced, bit 2 wide target
    float scale[4];
};
// The cbuffer's packing: view_sky_up and view_flags share one 16-byte row.
static_assert(offsetof(ViewRows, sky_up) == 128 && offsetof(ViewRows, view_flags) == 140 &&
              offsetof(ViewRows, scale) == 144 && sizeof(ViewRows) == 160);

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
    bool probes_done = false;
    std::vector<EyeMotion> eye_motions;        // an eye-space block's joints, in `joint_motion`'s place

    // Target passes drawn before the list's other passes (plan_early_targets): passes [first, end), and the
    // SCENE_BEGIN they sit after in the list, or none.
    struct EarlyTargets {
        std::uint32_t first = 0u, end = 0u, scene = NEBULA_NONE32;
    };
    bool targets_planned = false;
    std::vector<EarlyTargets> early_targets;
    // Probes asked right after an earlier pass (plan_early_probes), in list order.
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
    // Tables naming a freed texture go; their descriptor tables join `retired`.
    void forget_textures(std::span<const std::uint32_t> freed, std::vector<gpu::Table>& retired);

    [[nodiscard]] const GpuMesh& mesh(std::uint32_t id) const noexcept { return meshes_[id]; }
    [[nodiscard]] const GpuModel& model(std::uint32_t id) const noexcept { return models_[id]; }
    [[nodiscard]] const StaticSlot& slot(std::uint32_t id) const noexcept { return slots_[id]; }
    [[nodiscard]] const PipelineEntry& pipeline(std::uint32_t id) const noexcept { return pipelines_[id]; }
    void write_pipeline_list(const char* path);
    [[nodiscard]] std::uint32_t pipeline_count() const noexcept { return static_cast<std::uint32_t>(pipelines_.size()); }
    [[nodiscard]] TextureTable& table(std::uint32_t id) noexcept {
        return id < tables_.size() && tables_[id] != nullptr ? *tables_[id] : no_table_;
    }
    [[nodiscard]] std::span<const ao::ObjectSlot> object_slots(const GpuMesh& mesh) const noexcept;
    [[nodiscard]] std::uint64_t memory_bytes() noexcept;
#if defined(NEBULA_DEV)
    // The rows a registered slot holds, and what a registered mesh's stream holds, for the inspector.
    [[nodiscard]] const float* slot_rows(std::uint32_t id) const noexcept { return id < slot_rows_.size() ? slot_rows_[id] : nullptr; }
    [[nodiscard]] const std::byte* mesh_cpu(const GpuMesh& mesh, gpu::Address address) const noexcept {
        const gpu::UploadMemory& memory = arenas_[mesh.arena].memory;
        return memory.cpu + (address - memory.gpu);
    }
#endif

private:
    struct Arena {
        gpu::UploadMemory memory;
        std::size_t bytes = 0u;
        std::size_t used = 0u;
        std::uint32_t live = 0u;
    };
    struct Placed {
        std::byte* cpu = nullptr;
        gpu::Address gpu = 0u;
        std::uint32_t arena = 0u;
    };
    struct Retired {
        std::uint64_t fence = 0u;
        std::uint32_t arena = 0u;
        std::uint32_t model = 0u;
        std::uint32_t first_mesh = 0u, mesh_count = 0u;
        bool has_geometry = false;
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

    std::uint32_t next_model_ = 1u, next_mesh_ = 1u, next_slot_ = 0u, next_table_ = 1u;
    // Every pipeline the game registered, with its inputs (the entries' copies, in registration order).
    std::vector<PipelineEntry> all_pipelines_;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> pipeline_by_hash_;
    std::unordered_map<std::uint64_t, std::vector<TableKey>> table_by_hash_;
    std::unordered_map<std::uint64_t, std::vector<SlotRecord>> slot_by_hash_;

    gpu::UploadMemory slot_page_;
    std::size_t slot_page_used_ = 0u, slot_page_capacity_ = 0u;
    std::uint64_t slot_bytes_ = 0u;
    std::vector<gpu::UploadMemory> slot_pages_;
#if defined(NEBULA_DEV)
    std::vector<const float*> slot_rows_;
#endif
};

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
    float camera_move_ = 0.0f;
};

// A block's joints at t through its motions, `out` three rows each. An eye-space block's are eye matrices under `camera_t`.
void joints_at(const ListData& now, const NebulaFrameBlock& block, std::uint32_t block_index, float t, const Affine34* camera_t,
               float (*out)[4]) noexcept;

}  // namespace galaxy::render::core
