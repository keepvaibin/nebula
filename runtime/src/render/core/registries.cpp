// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/program_table.h"
#include "render/core/frame_core.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace galaxy::render::core {

namespace {

constexpr std::size_t kArenaPage = 64ull << 20u;
constexpr std::size_t kSlotPage = 1ull << 20u;
constexpr std::size_t kSlotAlign = 256u;

template <typename T>
const T* at(const void* file, std::uint32_t offset) noexcept {
    return reinterpret_cast<const T*>(static_cast<const std::byte*>(file) + offset);
}

std::uint64_t hash_bytes(const void* data, std::size_t size, std::uint64_t hash = 14695981039346656037ull) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ p[i]) * 1099511628211ull;
    }
    return hash;
}

// A mesh's bounds under each of its draw matrices, for the occluders.
void object_slots_of(const NebulaModelMesh& mesh, const std::byte* positions, const std::byte* matrices,
                     std::vector<ao::ObjectSlot>& out, std::uint32_t& first, std::uint32_t& count) {
    first = static_cast<std::uint32_t>(out.size());
    if (mesh.vertex_count == 0u) {
        count = 0u;
        return;
    }
    std::map<std::uint32_t, ao::ObjectSlot> slots;
    for (std::uint32_t v = 0; v < mesh.vertex_count; ++v) {
        std::uint32_t matrix = mesh.single_matrix;
        if (mesh.single_matrix == NEBULA_MODEL_NONE) {
            std::memcpy(&matrix, matrices + 4u * v, 4u);
        }
        float p[3];
        std::memcpy(p, positions + 12u * v, sizeof(p));
        const auto [it, fresh] = slots.try_emplace(matrix * kRowsPerMatrix);
        ao::ObjectSlot& slot = it->second;
        if (fresh) {
            slot.row = matrix * kRowsPerMatrix;
            std::copy_n(p, 3u, slot.min);
            std::copy_n(p, 3u, slot.max);
        } else {
            for (unsigned c = 0; c < 3u; ++c) {
                slot.min[c] = std::min(slot.min[c], p[c]);
                slot.max[c] = std::max(slot.max[c], p[c]);
            }
        }
    }
    for (const auto& [row, slot] : slots) {
        out.push_back(slot);
    }
    count = static_cast<std::uint32_t>(out.size()) - first;
}

}  // namespace

std::uint64_t Registries::memory_bytes() noexcept {
    const std::lock_guard guard(lock_);
    std::uint64_t bytes = 0u;
    for (const Arena& arena : arenas_) {
        bytes += arena.bytes;
    }
    return bytes + slot_bytes_;
}

bool Registries::arena_memory(std::uint32_t index, gpu::Address& base, std::size_t& bytes) noexcept {
    const std::lock_guard guard(lock_);
    if (index >= arenas_.size() || !arenas_[index].memory) {
        return false;
    }
    base = arenas_[index].memory.gpu;
    bytes = arenas_[index].bytes;
    return true;
}

Registries::MemoryStats Registries::memory_stats() noexcept {
    const std::lock_guard guard(lock_);
    MemoryStats stats;
    for (const Arena& arena : arenas_) {
        if (!arena.memory) {
            continue;
        }
        ++stats.pages;
        stats.empty_pages += arena.live == 0u ? 1u : 0u;
        stats.page_bytes += arena.bytes;
        stats.local_bytes += arena.memory.local ? arena.bytes : 0u;
        stats.live_bytes += arena.bytes - arena.free_bytes;
    }
    stats.slot_bytes = slot_bytes_;
    stats.slots = next_slot_;
    return stats;
}

void Registries::initialize(gpu::Device* device, DeviceGate& gate) {
    device_ = device;
    gate_ = &gate;
    arenas_.clear();
    meshes_.assign(1u, GpuMesh{});
    models_.assign(1u, GpuModel{});
    tables_.clear();
    tables_.resize(1u);
    slot_page_ = {};
    slot_page_used_ = 0u;
    slot_page_capacity_ = 0u;
    slot_bytes_ = 0u;
}

void Registries::shutdown() noexcept {
    arenas_.clear();
    meshes_.clear();
    models_.clear();
    tables_.clear();
    slot_pages_.clear();
    slot_page_ = {};
    device_ = nullptr;
}

// The fullest page with room takes the range (the lowest index on a tie), and inside it the smallest free range that fits:
// the lightly filled pages drain as their models retire and give_back frees them. (A bump cursor that rewound only when a
// whole page emptied let one model that lived on pin the page's every freed byte, and a scene's worth of new models
// opened another 64 MB page.)
Registries::Placed Registries::place(std::size_t bytes) {
    bytes = (std::max<std::size_t>(bytes, 256u) + 255u) & ~std::size_t{255u};
    constexpr std::uint32_t kNone = ~0u;
    std::uint32_t chosen = kNone;
    std::size_t chosen_offset = 0u;
    for (std::uint32_t i = 0; i < arenas_.size(); ++i) {
        const Arena& arena = arenas_[i];
        if (!arena.memory || arena.free_bytes < bytes) {
            continue;
        }
        if (chosen != kNone && arena.free_bytes >= arenas_[chosen].free_bytes) {
            continue;
        }
        const std::map<std::size_t, std::size_t>::const_iterator end = arena.free.end();
        std::map<std::size_t, std::size_t>::const_iterator fit = end;
        for (auto it = arena.free.begin(); it != end; ++it) {
            if (it->second >= bytes && (fit == end || it->second < fit->second)) {
                fit = it;
            }
        }
        if (fit != end) {
            chosen = i;
            chosen_offset = fit->first;
        }
    }
    if (chosen != kNone) {
        Arena& arena = arenas_[chosen];
        const auto range = arena.free.find(chosen_offset);
        const std::size_t range_size = range->second;
        arena.free.erase(range);
        if (range_size > bytes) {
            arena.free.emplace(chosen_offset + bytes, range_size - bytes);
        }
        arena.free_bytes -= bytes;
        ++arena.live;
        Placed placed{arena.memory.cpu + chosen_offset, arena.memory.gpu + chosen_offset, chosen, chosen_offset, bytes};
        return placed;
    }
    // A new page, in the place of one given back when there is one (a page's index is held by what is placed in it only).
    auto index = static_cast<std::uint32_t>(arenas_.size());
    for (std::uint32_t i = 0; i < arenas_.size(); ++i) {
        if (!arenas_[i].memory && arenas_[i].live == 0u) {
            index = i;
            break;
        }
    }
    const std::size_t page = std::max(bytes, kArenaPage);
    // Geometry is written once, in order, and read by the GPU every frame it draws: video memory where the device maps it.
    gpu::UploadMemory memory = gpu::create_upload_memory(device_, page, "Nebula Meshes", gpu::UploadPlace::GpuLocal);
    if (!memory) {
        throw std::runtime_error("no memory for meshes");
    }
    if (index == arenas_.size()) {
        arenas_.emplace_back();
    }
    Arena& arena = arenas_[index];
    arena.memory = std::move(memory);
    arena.bytes = page;
    arena.free.clear();
    if (page > bytes) {
        arena.free.emplace(bytes, page - bytes);
    }
    arena.free_bytes = page - bytes;
    arena.live = 1u;
    Placed placed{arena.memory.cpu, arena.memory.gpu, index, 0u, bytes};
    return placed;
}

void Registries::fill(const Placed& placed, const void* source, std::size_t bytes) noexcept {
    std::memcpy(placed.cpu, source, bytes);
}

void Registries::give_back(std::uint32_t index, std::size_t offset, std::size_t size) {
    if (index >= arenas_.size() || !arenas_[index].memory || size == 0u) {
        return;
    }
    Arena& arena = arenas_[index];
    std::size_t start = offset;
    std::size_t length = size;
    const auto next = arena.free.lower_bound(offset);
    if (next != arena.free.begin()) {
        const auto previous = std::prev(next);
        if (previous->first + previous->second == start) {
            start = previous->first;
            length += previous->second;
            arena.free.erase(previous);
        }
    }
    if (next != arena.free.end() && offset + size == next->first) {
        length += next->second;
        arena.free.erase(next);
    }
    arena.free.emplace(start, length);
    arena.free_bytes += size;
    if (arena.live != 0u) {
        --arena.live;
    }
    if (arena.live != 0u) {
        return;
    }
    // The page is empty (the GPU is past everything that read it: reclaim runs after the retiring fence). A page of an
    // unusual size, or a second empty one, gives its memory back; one empty page stays for the next scene.
    bool release = arena.bytes != kArenaPage;
    for (std::uint32_t other = 0; other < arenas_.size() && !release; ++other) {
        release = other != index && arenas_[other].memory && arenas_[other].live == 0u;
    }
    if (release) {
        arena.memory = {};
        arena.bytes = 0u;
        arena.free.clear();
        arena.free_bytes = 0u;
    }
}

std::uint32_t Registries::take_mesh_ids(std::uint32_t count) {
    if (count != 0u) {
        const auto run = free_mesh_runs_.find(count);
        if (run != free_mesh_runs_.end() && !run->second.empty()) {
            const std::uint32_t first = run->second.back();
            run->second.pop_back();
            return first;
        }
    }
    const std::uint32_t first = next_mesh_;
    next_mesh_ += count;
    return first;
}

namespace {

PipelineEntry entry_of(const NebulaPipelineDesc& desc, const NebulaLayoutInput* inputs) {
    PipelineEntry entry{desc, {}, program_reads_world(desc.vertex), program_lit(desc.pixel)};
    if (desc.input_count != 0u) {
        entry.inputs.assign(inputs, inputs + desc.input_count);
    }
    for (const NebulaLayoutInput& input : entry.inputs) {
        if (input.semantic == NEBULA_INPUT_MATRIX && input.stream == 0u && input.format == 3u) {
            entry.matrix_at = input.offset;
        }
    }
    return entry;
}

// A pipeline's identity: its description and its inputs, field by field.
std::uint64_t pipeline_hash(const NebulaPipelineDesc& desc, const NebulaLayoutInput* inputs) {
    const std::uint64_t head[] = {desc.vertex, desc.pixel, (std::uint64_t{desc.state} << 32u) | desc.input_count, desc.position_stride};
    std::uint64_t hash = hash_bytes(head, sizeof(head));
    for (std::uint32_t i = 0; i < desc.input_count; ++i) {
        const std::uint64_t word[] = {(std::uint64_t{inputs[i].semantic} << 16u) | (std::uint64_t{inputs[i].stream} << 8u) | inputs[i].format,
                                      (std::uint64_t{inputs[i].offset} << 32u) | inputs[i].stride};
        hash = hash_bytes(word, sizeof(word), hash);
    }
    return hash;
}

bool same_pipeline(const PipelineEntry& entry, const NebulaPipelineDesc& desc, const NebulaLayoutInput* inputs) {
    if (entry.desc.vertex != desc.vertex || entry.desc.pixel != desc.pixel || entry.desc.state != desc.state ||
        entry.desc.input_count != desc.input_count || entry.desc.position_stride != desc.position_stride) {
        return false;
    }
    for (std::uint32_t i = 0; i < desc.input_count; ++i) {
        const NebulaLayoutInput& a = entry.inputs[i];
        const NebulaLayoutInput& b = inputs[i];
        if (a.semantic != b.semantic || a.stream != b.stream || a.format != b.format || a.offset != b.offset || a.stride != b.stride) {
            return false;
        }
    }
    return true;
}

// What a model's file says, with no device involved.
void read_model(const void* file, GpuModel& model, std::vector<GpuMesh>& meshes) {
    const auto& header = *static_cast<const NebulaModelHeader*>(file);
    if (header.draw_matrix_count != 0u) {
        const auto* draw = at<NebulaModelDrawMatrix>(file, header.draw_matrices_at);
        model.draw_matrices.assign(draw, draw + header.draw_matrix_count);
    }
    if (header.envelope_count != 0u) {
        const auto* envelopes = at<NebulaModelEnvelope>(file, header.envelopes_at);
        model.envelopes.assign(envelopes, envelopes + header.envelope_count);
    }
    if (header.mix_count != 0u) {
        const auto* mixes = at<NebulaModelMix>(file, header.mixes_at);
        model.mixes.assign(mixes, mixes + header.mix_count);
    }
    if (header.joint_count != 0u) {
        const auto* joints = at<NebulaModelJoint>(file, header.joints_at);
        model.joints.assign(joints, joints + header.joint_count);
        if (header.envelope_count != 0u) {
            const auto* binds = at<float>(file, header.inverse_binds_at);
            model.inverse_binds.assign(binds, binds + 12u * header.joint_count);
        }
    }
    model.tables.draw_matrix_count = header.draw_matrix_count;
    model.tables.joint_count = header.joint_count;
    model.tables.has_billboard = (header.flags & NEBULA_MODEL_HAS_BILLBOARD) != 0u;
    model.mesh_count = header.mesh_count;

    meshes.resize(header.mesh_count);
    // c86 (PLAN-LIGHTS L2): each glowing mesh's light shapes (version 10 on; the lights pass shades them).
    model.glow_shapes.clear();
    if (header.version >= 10u && header.glow_shapes_at != 0u) {
        const auto* spans = at<NebulaModelGlowSpan>(file, header.glow_shapes_at);
        const auto* shapes = reinterpret_cast<const NebulaModelGlowShape*>(spans + header.mesh_count);
        std::uint32_t total = 0u;
        for (std::uint32_t m = 0; m < header.mesh_count; ++m) {
            total = std::max(total, spans[m].first + spans[m].count);
        }
        model.glow_shapes.assign(shapes, shapes + total);
        for (std::uint32_t m = 0; m < header.mesh_count; ++m) {
            meshes[m].glow_shape_first = spans[m].first;
            meshes[m].glow_shape_count = spans[m].count;
        }
    }
    const auto* file_meshes = at<NebulaModelMesh>(file, header.meshes_at);
    const auto* hits = header.hits_at != 0u ? at<NebulaModelHit>(file, header.hits_at) : nullptr;
    const auto* paint_lights = header.paint_lights_at != 0u ? at<NebulaModelPaintLight>(file, header.paint_lights_at) : nullptr;
    for (std::uint32_t m = 0; m < header.mesh_count; ++m) {
        const NebulaModelMesh& mesh = file_meshes[m];
        GpuMesh& gpu_mesh = meshes[m];
        if (hits != nullptr && mesh.material < header.material_count) {
            const NebulaModelHit& hit = hits[mesh.material];
            const NebulaModelMaterial& material = at<NebulaModelMaterial>(file, header.materials_at)[mesh.material];
            gpu_mesh.hit_class = hit.klass;
            gpu_mesh.hit_flags = hit.flags;
            std::copy_n(hit.emission, 3u, gpu_mesh.hit_emission);
            std::copy_n(hit.base, 3u, gpu_mesh.hit_base);
            gpu_mesh.hit_roughness = hit.roughness;
            gpu_mesh.hit_metal = hit.metal;
            gpu_mesh.hit_transmission = hit.transmission;
            gpu_mesh.hit_environment = hit.environment_share;
            std::copy_n(hit.glow, 4u, gpu_mesh.glow);
            gpu_mesh.glow_source = static_cast<std::uint8_t>(hit.glow_source & 31u);  // bits 5-7: why it glows (c81)
            gpu_mesh.hit_ior = hit.ior;
            gpu_mesh.hit_thickness = hit.thickness;
            std::copy_n(hit.env_tint, 3u, gpu_mesh.hit_env_tint);
            gpu_mesh.hit_env_map = hit.env_map;
            gpu_mesh.hit_body = hit.body;
            gpu_mesh.hit_shine = hit.shine;
            gpu_mesh.hit_tint_keep = hit.tint_keep;
            gpu_mesh.hit_through = static_cast<float>(hit.through) / 255.0f;
            // Where glow_source's colour sits among the rows of the material's own stages (the first form that
            // replaces nothing), and its cooked value: registers r (code r) are signed, the others bytes, all /255
            // as Material::fillRow fills them.
            const std::uint32_t code = gpu_mesh.glow_source - 1u;
            if (gpu_mesh.glow_source >= 1u && gpu_mesh.glow_source <= 9u && material.forms_at != 0u) {
                const auto* forms = at<NebulaModelForm>(file, material.forms_at);
                for (std::uint32_t f = 0; f < material.form_count; ++f) {
                    const NebulaModelForm& form = forms[f];
                    if (form.replace != 0u) {
                        continue;
                    }
                    const auto* codes = at<std::uint32_t>(file, form.pixel_rows_at);
                    for (std::uint32_t p = 0; p < form.pixel_row_count; ++p) {
                        if (codes[p] == code && form.vertex_row_count + p < 0xFFFFu &&
                            form.vertex_row_count + form.pixel_row_count < 0xFFFFu) {
                            gpu_mesh.glow_row = static_cast<std::uint16_t>(form.vertex_row_count + p);
                            gpu_mesh.glow_row_count = static_cast<std::uint16_t>(form.vertex_row_count + form.pixel_row_count);
                            break;
                        }
                    }
                    break;
                }
                for (unsigned c = 0; c < 3u; ++c) {
                    const float value = code <= NEBULA_ROW_REGISTER + 2u ? static_cast<float>(material.registers[code][c])
                                        : code < NEBULA_ROW_MATERIAL_COLOR
                                            ? static_cast<float>(material.constants[code - NEBULA_ROW_CONSTANT][c])
                                            : static_cast<float>(material.material_color[code - NEBULA_ROW_MATERIAL_COLOR][c]);
                    gpu_mesh.glow_cooked[c] = value / 255.0f;
                }
            }
            // Map 0's coordinate (plan 13.3): the form's texture table's first map, the first stage that samples it,
            // its coordinate when made from a uv set by a matrix (type 1, sources 4 to 19; not the form's normal
            // coordinate), and that coordinate's u and v rows among the form's vertex rows.
            if (material.forms_at != 0u && material.coords_at != 0u && hit.stages_at != 0u) {
                const auto* forms = at<NebulaModelForm>(file, material.forms_at);
                for (std::uint32_t f = 0; f < material.form_count; ++f) {
                    const NebulaModelForm& form = forms[f];
                    if (form.replace != 0u) {
                        continue;
                    }
                    if (form.texture_count != 0u && form.vertex_row_count + form.pixel_row_count < 0xFFFFu) {
                        // A map's coordinate rows: the first of its u and v rows among the form's vertex rows and the
                        // uv set it reads, or none (FB b90: map 0's and map 1's alike).
                        const auto map_rows = [&](std::uint8_t map, std::uint16_t& row, std::uint8_t& uv) {
                            const auto* stages = at<NebulaModelStage>(file, hit.stages_at);
                            std::uint32_t coord = 0xFFu;
                            for (std::uint32_t s = 0; s < hit.stage_count && coord == 0xFFu; ++s) {
                                if (stages[s].map == map) {
                                    coord = stages[s].coord;
                                }
                            }
                            const bool normal_coord = (form.flags & 1u) != 0u && coord == 0u;
                            if (coord >= material.coord_count || normal_coord) {
                                return;
                            }
                            const NebulaModelCoord& c = at<NebulaModelCoord>(file, material.coords_at)[coord];
                            if (c.type != 1u || c.source < 4u || c.source > 19u) {
                                return;
                            }
                            const auto* codes = at<std::uint32_t>(file, form.vertex_rows_at);
                            const std::uint32_t coord_code = NEBULA_ROW_COORD + 3u * coord;
                            for (std::uint32_t r = 0; r + 1u < form.vertex_row_count; ++r) {
                                if (codes[r] == coord_code && codes[r + 1u] == coord_code + 1u) {
                                    row = static_cast<std::uint16_t>(r);
                                    uv = static_cast<std::uint8_t>(c.source - 4u);
                                    return;
                                }
                            }
                        };
                        const auto* maps = at<std::uint8_t>(file, form.textures_at);
                        map_rows(maps[0], gpu_mesh.map0_row, gpu_mesh.map0_uv);
                        if (gpu_mesh.map0_row != 0xFFFFu) {
                            gpu_mesh.map0_form_rows = static_cast<std::uint16_t>(form.vertex_row_count + form.pixel_row_count);
                        }
                        if (form.texture_count >= 2u && maps[1] != maps[0]) {
                            map_rows(maps[1], gpu_mesh.map1_row, gpu_mesh.map1_uv);
                            if (gpu_mesh.map1_row != 0xFFFFu) {
                                gpu_mesh.map1_slot = 1u;
                            }
                        }
                    }
                    break;
                }
            }
            // The alpha test's first comparison, where it is "greater" or "greater or equal" (4, 6): its reference.
            if (material.alpha_comp0 == 4u || material.alpha_comp0 == 6u) {
                gpu_mesh.hit_alpha = std::max(static_cast<float>(material.alpha_ref0) / 255.0f, 1.0f / 255.0f);
            }
        }
        if (paint_lights != nullptr) {
            gpu_mesh.paint_share = paint_lights[m].share;
            std::copy_n(paint_lights[m].direction, 2u, gpu_mesh.paint_direction);
        }
        gpu_mesh.vertex_count = mesh.vertex_count;
        gpu_mesh.refined_vertex_count = mesh.refined_vertex_count;
        gpu_mesh.index_count = mesh.index_count;
        gpu_mesh.refined_index_count = mesh.refined_index_count;
        gpu_mesh.further_stride = mesh.further_stride;
        gpu_mesh.single_matrix = mesh.single_matrix;
        gpu_mesh.index_bytes = mesh.index_bytes;
        std::copy_n(mesh.min, 3u, gpu_mesh.min);
        std::copy_n(mesh.max, 3u, gpu_mesh.max);
        object_slots_of(mesh, at<std::byte>(file, mesh.positions_at),
                        mesh.matrices_at != 0u ? at<std::byte>(file, mesh.matrices_at) : nullptr, model.object_slots,
                        gpu_mesh.object_slots_first, gpu_mesh.object_slots_count);
    }
}

}  // namespace

void Registries::place_model(std::uint32_t id, const LiveModel& live, GpuModel model, std::vector<GpuMesh>& meshes, const Placed& placed) {
    const auto& header = *static_cast<const NebulaModelHeader*>(live.file);
    const auto* file_meshes = at<NebulaModelMesh>(live.file, header.meshes_at);
    model.first_mesh = live.first_mesh;
    model.arena = placed.arena;
    model.arena_offset = placed.offset;
    model.arena_size = placed.size;
    const auto stream = [&](std::uint32_t offset) -> gpu::Address {
        return offset != 0u ? placed.gpu + (offset - header.geometry_at) : gpu::Address{0u};
    };
    for (std::uint32_t m = 0; m < header.mesh_count; ++m) {
        const NebulaModelMesh& mesh = file_meshes[m];
        GpuMesh& gpu_mesh = meshes[m];
        gpu_mesh.positions = stream(mesh.positions_at);
        gpu_mesh.billboard = mesh.billboard;
        gpu_mesh.attributes = stream(mesh.attributes_at);
        gpu_mesh.matrices = stream(mesh.matrices_at);
        gpu_mesh.further = stream(mesh.further_at);
        gpu_mesh.indices = stream(mesh.indices_at);
        gpu_mesh.refined_indices = stream(mesh.refined_indices_at);
        // D2: each vertex's last word of stream 3 is its de-light gain (c37); a ray's hit reads it (FB b54).
        gpu_mesh.gains = (mesh.attributes & NEBULA_MESH_GAIN) != 0u && mesh.further_at != 0u && mesh.further_stride >= 4u
                             ? stream(mesh.further_at) + (mesh.further_stride - 4u)
                             : gpu::Address{0u};
        gpu_mesh.arena = placed.arena;
        gpu_mesh.model = id;
        incoming_.meshes.emplace_back(live.first_mesh + m, gpu_mesh);
    }
    incoming_.models.emplace_back(id, std::move(model));
}

std::uint32_t Registries::model_register(const void* file, std::uint32_t* first_mesh) {
    return gate_->use([&] {
        const auto& header = *static_cast<const NebulaModelHeader*>(file);
        GpuModel model;
        std::vector<GpuMesh> meshes;
        read_model(file, model, meshes);

        Placed placed{};
        std::uint32_t id;
        {
            const std::lock_guard guard(lock_);
            placed = header.geometry_size != 0u ? place(header.geometry_size) : Placed{};
            if (!free_models_.empty()) {
                id = free_models_.back();
                free_models_.pop_back();
            } else {
                id = next_model_++;
            }
            const LiveModel live{file, take_mesh_ids(header.mesh_count)};
            live_models_[id] = live;
            place_model(id, live, std::move(model), meshes, placed);
            if (first_mesh != nullptr) {
                *first_mesh = live.first_mesh;
            }
        }
        if (header.geometry_size != 0u) {
            fill(placed, at<std::byte>(file, header.geometry_at), header.geometry_size);
        }
        return id;
    });
}

void Registries::model_free(std::uint32_t model) {
    const auto hold = gate_->hold();
    const std::lock_guard guard(lock_);
    live_models_.erase(model);
    incoming_.freed_models.push_back(model);
}

void Registries::place_mesh(std::uint32_t id, const LiveMesh& live) {
    const std::size_t bytes = std::size_t{live.vertex_bytes} + std::size_t{live.index_count} * 4u;
    const Placed placed = place(bytes);
    fill(placed, live.bytes.get(), bytes);
    GpuMesh gpu_mesh;
    gpu_mesh.positions = placed.gpu;
    gpu_mesh.indices = live.index_count != 0u ? placed.gpu + live.vertex_bytes : gpu::Address{0u};
    gpu_mesh.vertex_count = live.stride != 0u ? live.vertex_bytes / live.stride : 0u;
    gpu_mesh.index_count = live.index_count;
    gpu_mesh.site_stride = live.stride;
    gpu_mesh.arena = placed.arena;
    gpu_mesh.arena_offset = placed.offset;
    gpu_mesh.arena_size = placed.size;
    incoming_.meshes.emplace_back(id, gpu_mesh);
}

std::uint32_t Registries::mesh_register(const NebulaFrameMesh& mesh) {
    return gate_->use([&] {
        const std::size_t index_bytes = std::size_t{mesh.index_count} * 4u;
        LiveMesh live;
        live.bytes = std::make_unique<std::byte[]>(std::size_t{mesh.vertex_bytes} + index_bytes);
        std::memcpy(live.bytes.get(), mesh.vertices, mesh.vertex_bytes);
        if (mesh.index_count != 0u) {
            std::memcpy(live.bytes.get() + mesh.vertex_bytes, mesh.indices, index_bytes);
        }
        live.vertex_bytes = mesh.vertex_bytes;
        live.stride = mesh.stride;
        live.index_count = mesh.index_count;
        const std::lock_guard guard(lock_);
        const std::uint32_t id = take_mesh_ids(1u);
        place_mesh(id, live);
        live_meshes_[id] = std::move(live);
        return id;
    });
}

void Registries::mesh_free(std::uint32_t mesh) {
    const auto hold = gate_->hold();
    const std::lock_guard guard(lock_);
    live_meshes_.erase(mesh);
    incoming_.freed_meshes.push_back(mesh);
}

void Registries::place_slot(const float (*rows)[4], std::uint32_t vertex_rows, std::uint32_t pixel_rows) {
    const auto take = [this](std::size_t bytes) {
        bytes = (std::max<std::size_t>(bytes, 16u) + kSlotAlign - 1u) & ~(kSlotAlign - 1u);
        if (!slot_page_ || slot_page_used_ + bytes > slot_page_capacity_) {
            slot_page_capacity_ = std::max(bytes, kSlotPage);
            slot_page_ = gpu::create_upload_memory(device_, slot_page_capacity_, "Nebula Material Slots");
            if (!slot_page_) {
                throw std::runtime_error("no memory for material slots");
            }
            slot_pages_.push_back(slot_page_);
            slot_page_used_ = 0u;
            slot_bytes_ += slot_page_capacity_;
        }
        const std::size_t at = slot_page_used_;
        slot_page_used_ += bytes;
        return at;
    };
    const std::size_t vertex_at = take(std::size_t{vertex_rows} * 16u);
    gpu::UploadMemory vertex_page = slot_page_;
    std::memcpy(vertex_page.cpu + vertex_at, rows, std::size_t{vertex_rows} * 16u);
    const std::size_t pixel_at = take(std::size_t{pixel_rows} * 16u);
    std::memcpy(slot_page_.cpu + pixel_at, rows + vertex_rows, std::size_t{pixel_rows} * 16u);

    StaticSlot slot;
    slot.vertex_rows = vertex_page.gpu + vertex_at;
    slot.pixel_rows = slot_page_.gpu + pixel_at;
    slot.cpu = &rows[0][0];  // the record's copy (material_register_locked, restart): kept for the registries' life
    incoming_.slots.push_back(slot);
}

void Registries::lose_device() noexcept {
    const std::lock_guard guard(lock_);
    arenas_.clear();
    retired_.clear();
    held_freed_models_.clear();
    held_freed_meshes_.clear();
    slot_pages_.clear();
    slot_page_ = {};
    slot_page_used_ = 0u;
    slot_page_capacity_ = 0u;
    slot_bytes_ = 0u;
    incoming_ = RegistryOps{};
    no_table_ = TextureTable{};
    device_ = nullptr;
}

void Registries::restart(gpu::Device* device) {
    const std::lock_guard guard(lock_);
    device_ = device;
    meshes_.assign(1u, GpuMesh{});
    models_.assign(1u, GpuModel{});
    slots_.clear();
    pipelines_.clear();
    tables_.clear();
    tables_.resize(1u);

    for (const auto& [id, live] : live_models_) {
        const auto& header = *static_cast<const NebulaModelHeader*>(live.file);
        GpuModel model;
        std::vector<GpuMesh> meshes;
        read_model(live.file, model, meshes);
        const Placed placed = header.geometry_size != 0u ? place(header.geometry_size) : Placed{};
        place_model(id, live, std::move(model), meshes, placed);
        if (header.geometry_size != 0u) {
            fill(placed, at<std::byte>(live.file, header.geometry_at), header.geometry_size);
        }
    }
    for (const auto& [id, live] : live_meshes_) {
        place_mesh(id, live);
    }

    // Slots, pipelines and tables take their places in id order.
    std::vector<const SlotRecord*> records(next_slot_, nullptr);
    for (const auto& [hash, bucket] : slot_by_hash_) {
        for (const SlotRecord& record : bucket) {
            records[record.id] = &record;
        }
    }
    for (const SlotRecord* record : records) {
        place_slot(reinterpret_cast<const float (*)[4]>(record->cpu.get()), record->vertex_rows, record->pixel_rows);
    }
    for (const PipelineEntry& entry : all_pipelines_) {
        incoming_.pipelines.push_back(entry);
    }
    std::vector<const TableKey*> keys(next_table_, nullptr);
    for (const auto& [hash, bucket] : table_by_hash_) {
        for (const TableKey& key : bucket) {
            keys[key.id] = &key;
        }
    }
    for (std::uint32_t id = 1u; id < next_table_; ++id) {
        TextureTable& table = incoming_.tables.emplace_back();
        if (keys[id] != nullptr) {
            table.count = keys[id]->count;
            table.list = keys[id]->list;
        }
    }
}

std::uint32_t Registries::material_register(const float (*rows)[4], std::uint32_t vertex_rows, std::uint32_t pixel_rows) {
    return gate_->use([&] { return material_register_locked(rows, vertex_rows, pixel_rows); });
}

std::uint32_t Registries::material_register_locked(const float (*rows)[4], std::uint32_t vertex_rows, std::uint32_t pixel_rows) {
    const std::size_t count = std::size_t{vertex_rows} + pixel_rows;
    const std::uint64_t hash = hash_bytes(rows, count * 16u, (std::uint64_t{vertex_rows} << 32u) ^ pixel_rows);
    const std::lock_guard guard(lock_);
    auto& bucket = slot_by_hash_[hash];
    for (const SlotRecord& record : bucket) {
        if (record.vertex_rows == vertex_rows && record.pixel_rows == pixel_rows &&
            std::memcmp(record.cpu.get(), rows, count * 16u) == 0) {
            return record.id;
        }
    }
    const std::uint32_t id = next_slot_++;
    // Compares read the rows back from the mapped page's copy kept here: the page is write-combined. The slot points
    // at this copy too (StaticSlot::cpu): it lives as long as the registries.
    SlotRecord record;
    record.vertex_rows = vertex_rows;
    record.pixel_rows = pixel_rows;
    record.id = id;
    record.cpu = std::make_unique<float[]>(std::max<std::size_t>(count, 1u) * 4u);
    std::memcpy(record.cpu.get(), rows, count * 16u);
    place_slot(reinterpret_cast<const float (*)[4]>(record.cpu.get()), vertex_rows, pixel_rows);
    bucket.push_back(std::move(record));
    return id;
}

std::uint32_t Registries::pipeline_register(const NebulaPipelineDesc& desc, const NebulaLayoutInput* inputs) {
    const std::uint64_t hash = pipeline_hash(desc, inputs);
    const std::lock_guard guard(lock_);
    auto& bucket = pipeline_by_hash_[hash];
    for (const std::uint32_t id : bucket) {
        if (same_pipeline(all_pipelines_[id], desc, inputs)) {
            return id;
        }
    }
    const std::uint32_t id = static_cast<std::uint32_t>(all_pipelines_.size());
    all_pipelines_.push_back(entry_of(desc, inputs));
    registered_pipelines_.store(static_cast<std::uint32_t>(all_pipelines_.size()), std::memory_order_release);
    incoming_.pipelines.push_back(all_pipelines_.back());
    bucket.push_back(id);
    return id;
}

// Pipelines.bin's format (nebula_frame_list.h NEBULA_PIPELINE_FILE_MAGIC).
void Registries::write_pipeline_list(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::lock_guard guard(lock_);
    const auto put = [&out](const void* bytes, std::size_t size) { out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size)); };
    const std::uint32_t count = static_cast<std::uint32_t>(all_pipelines_.size());
    const std::uint32_t reserved = 0u;
    put(NEBULA_PIPELINE_FILE_MAGIC, 8u);
    put(&count, 4u);
    put(&reserved, 4u);
    for (const PipelineEntry& entry : all_pipelines_) {
        const NebulaPipelineDesc& desc = entry.desc;
        put(&desc.vertex, 8u);
        put(&desc.pixel, 8u);
        put(&desc.state, 4u);
        put(&desc.input_count, 4u);
        put(&desc.position_stride, 4u);
        put(entry.inputs.data(), entry.inputs.size() * sizeof(NebulaLayoutInput));
    }
}

std::uint32_t Registries::texture_table(const NebulaFrameTexture* list, std::uint32_t count) {
    const std::uint64_t hash = hash_bytes(list, count * sizeof(NebulaFrameTexture), count);
    const std::lock_guard guard(lock_);
    auto& bucket = table_by_hash_[hash];
    for (const TableKey& key : bucket) {
        if (key.count == count && std::memcmp(key.list.data(), list, count * sizeof(NebulaFrameTexture)) == 0) {
            return key.id;
        }
    }
    const std::uint32_t id = next_table_++;
    TableKey& key = bucket.emplace_back();
    key.id = id;
    key.count = count;
    key.list.assign(list, list + count);
    TextureTable& table = incoming_.tables.emplace_back();
    table.count = count;
    table.list = key.list;
    return id;
}

RegistryOps Registries::take_ops() {
    const std::lock_guard guard(lock_);
    return std::exchange(incoming_, RegistryOps{});
}

void Registries::apply(RegistryOps& ops, std::uint64_t submitted_fence) {
    for (auto& [id, model] : ops.models) {
        if (models_.size() <= id) {
            models_.resize(id + 1u);
        }
        GpuModel& placed = models_[id];
        placed = std::move(model);
        placed.tables.joints = placed.joints.data();
        placed.tables.draw_matrices = placed.draw_matrices.data();
        placed.tables.envelopes = placed.envelopes.data();
        placed.tables.mixes = placed.mixes.data();
        placed.tables.inverse_binds = reinterpret_cast<const float (*)[4]>(placed.inverse_binds.data());
    }
    for (const auto& [id, mesh] : ops.meshes) {
        if (meshes_.size() <= id) {
            meshes_.resize(id + 1u);
        }
        meshes_[id] = mesh;
    }
    slots_.insert(slots_.end(), ops.slots.begin(), ops.slots.end());
    pipelines_.insert(pipelines_.end(), ops.pipelines.begin(), ops.pipelines.end());
    for (const TextureTable& table : ops.tables) {
        tables_.push_back(std::make_unique<TextureTable>(table));
    }
    // The last list has walked for good: what it freed retires now, at the work submitted so far.
    for (const std::uint32_t id : held_freed_models_) {
        const GpuModel& model = models_[id];
        retired_.push_back({submitted_fence, model.arena, id, model.first_mesh, model.mesh_count, model.arena_size != 0u,
                            model.arena_offset, model.arena_size});
        for (std::uint32_t m = 0; tell_retired_meshes_ && m < model.mesh_count; ++m) {
            retired_meshes_.push_back(model.first_mesh + m);
        }
    }
    for (const std::uint32_t id : held_freed_meshes_) {
        const GpuMesh& mesh = meshes_[id];
        retired_.push_back({submitted_fence, mesh.arena, 0u, id, 1u, mesh.arena_size != 0u, mesh.arena_offset, mesh.arena_size});
        if (tell_retired_meshes_) {
            retired_meshes_.push_back(id);
        }
    }
    held_freed_models_ = std::move(ops.freed_models);
    held_freed_meshes_ = std::move(ops.freed_meshes);
    ops = RegistryOps{};
}

void Registries::reclaim(std::uint64_t completed_fence) {
    if (retired_.empty()) {
        return;
    }
    const std::lock_guard guard(lock_);
    std::erase_if(retired_, [&](const Retired& retired) {
        if (retired.fence > completed_fence) {
            return false;
        }
        if (retired.has_geometry) {
            give_back(retired.arena, retired.arena_offset, retired.arena_size);
        }
        for (std::uint32_t m = 0; m < retired.mesh_count; ++m) {
            meshes_[retired.first_mesh + m] = GpuMesh{};
        }
        if (retired.mesh_count != 0u) {
            free_mesh_runs_[retired.mesh_count].push_back(retired.first_mesh);
        }
        if (retired.model != 0u) {
            models_[retired.model] = GpuModel{};
            free_models_.push_back(retired.model);
        }
        return true;
    });
}

void Registries::forget_textures(std::span<const std::uint32_t> freed, std::vector<gpu::Table>& retired) {
    if (freed.empty()) {
        return;
    }
    const std::unordered_set<std::uint32_t> gone(freed.begin(), freed.end());
    std::vector<std::uint32_t> dropped;
    {
        const std::lock_guard guard(lock_);
        for (auto it = table_by_hash_.begin(); it != table_by_hash_.end();) {
            std::erase_if(it->second, [&](const TableKey& key) {
                for (std::uint32_t i = 0; i < key.count; ++i) {
                    if (gone.contains(key.list[i].texture)) {
                        dropped.push_back(key.id);
                        return true;
                    }
                }
                return false;
            });
            it = it->second.empty() ? table_by_hash_.erase(it) : std::next(it);
        }
    }
    for (const std::uint32_t id : dropped) {
        if (id < tables_.size() && tables_[id] != nullptr) {
            if (!tables_[id]->has_target && tables_[id]->textures.ptr != 0u) {
                retired.push_back(tables_[id]->textures);
            }
            tables_[id].reset();
        }
    }
}

void Registries::retire_idle_tables(std::uint64_t frame, std::vector<gpu::Table>& retired, std::uint64_t idle_frames) {
    for (const std::unique_ptr<TextureTable>& entry : tables_) {
        TextureTable* const table = entry.get();
        if (table == nullptr || table->retired || frame < table->last_frame || frame - table->last_frame <= idle_frames) {
            continue;
        }
        // A table holding a target lives for one frame (texture_table_for): its set is the renderer's to recycle.
        if (!table->has_target && table->textures.ptr != 0u) {
            retired.push_back(table->textures);
        }
        table->textures = {};
        table->samplers = {};
        table->lists = 0u;
        table->list_first = 0u;
        table->walk_stamp = ~0ull;
        table->textures_revision = ~0ull;
        table->samplers_generation = 0u;
        std::vector<std::uint32_t>().swap(table->sampler_slots);
        table->retired = true;
        ++table_retirements_;
    }
}

Registries::TableCounts Registries::table_counts() const noexcept {
    TableCounts counts;
    counts.retirements = table_retirements_;
    for (const std::unique_ptr<TextureTable>& table : tables_) {
        if (table != nullptr) {
            ++(table->retired ? counts.retired : counts.live);
        }
    }
    return counts;
}

std::span<const ao::ObjectSlot> Registries::object_slots(const GpuMesh& mesh) const noexcept {
    if (mesh.object_slots_count == 0u) {
        return {};
    }
    return std::span<const ao::ObjectSlot>(models_[mesh.model].object_slots)
        .subspan(mesh.object_slots_first, mesh.object_slots_count);
}

}  // namespace galaxy::render::core
