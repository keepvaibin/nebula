// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The tracer's scene: the instances a frame's draws name, and one top-level structure over them, built in the frame's
// commands before anything traces. The instances are in the scene camera's eye space, as the frame's rows are, so a
// ray starts at the eye's origin and the lights of a light set need no change. No API here (rt_backend.h builds).

#include "galaxy/render/rt/rt_backend.h"

#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::render::rt {

// One instance, in the layout Direct3D 12 and Vulkan both take.
struct Instance {
    float transform[3][4];      // eye from the mesh's own space
    std::uint32_t id_mask;      // id in the low 24 bits (the frame's item), mask in the high 8
    std::uint32_t flags;        // high 8 bits: kInstance*; low 24: kKind*, which a ray reads back at a hit
    std::uint64_t structure;    // the bottom-level structure's name
};
static_assert(sizeof(Instance) == 64u);

// Which rays see an instance.
inline constexpr std::uint32_t kMaskOpaque = 1u;   // writes depth unblended: stops light
inline constexpr std::uint32_t kMaskBlended = 2u;  // drawn blended or without depth: seen, casts no hard shadow
inline constexpr std::uint32_t kMaskEarly = 4u;    // opaque and drawn before the scene's opaque end: what the depth holds there
inline constexpr std::uint32_t kMaskSolid = 8u;    // opaque and no cutout: every triangle of it stops light (a shadow ray's)
inline constexpr std::uint32_t kMaskLit = 16u;     // solid and drawn by a lit program: what painted light could not know of
inline constexpr std::uint32_t kMaskSky = 64u;     // the sky's dome: stops no light, is what a mirror shows where nothing else stands
inline constexpr std::uint32_t kMaskCutout = 32u;  // opaque where its texture's alpha says so (an alpha test): a ray asks at each hit
inline constexpr std::uint32_t kMaskShadowOnly = 128u;  // drawn for the tracer alone (a traced draw): only shadow rays meet it
// What an instance is, read back at a hit (the instance's contribution field, which inline rays do not use).
inline constexpr std::uint32_t kKindCutout = 1u;   // its pixel program may discard (an alpha test): a hit is not yet a surface
inline constexpr std::uint32_t kKindCard = 4u;     // a billboard's sun card (l83): never opaque, so a ray can pass its own
inline constexpr std::uint32_t kKindClipped = 2u;  // with kKindCutout: a map part its clip areas show (c27), every hit asked
// Instance flags (the same bits in both APIs).
inline constexpr std::uint32_t kInstanceNoCull = 1u;  // a ray that culls back faces meets both of this instance's
inline constexpr std::uint32_t kInstanceFlip = 2u;    // its triangles face the other way
inline constexpr std::uint32_t kInstanceNotOpaque = 8u;  // a hit on it is a candidate the ray's shader decides (a cutout)
inline constexpr std::uint32_t kInstanceNoMicromaps = 32u;  // its structure's opacity micromap is not this draw's (5.3 G-2)
// What an instance has of its structure's opacity micromap (5.3 G-2): none under it; this draw's own (the micromap
// decides where its cutout is, asking only where it cannot tell); another's (turned off for this instance).
enum class Micromapped : std::uint8_t { None, Own, Other };

// Which faces a draw shows. A ray that culls back faces (the views; a ray that asks what the scene drew) meets the
// same faces as the draw; a ray without that flag (a shadow's) meets every face.
enum class Faces : std::uint8_t { Both, Front, Back };

// What a frame's scene held (--diagnostics rt-inputs).
struct SceneCount {
    std::uint32_t instances = 0u, opaque = 0u, blended = 0u;
    std::uint32_t waiting = 0u;      // meshes with no structure yet
    std::uint32_t skinned = 0u;      // draws whose vertices carry their own matrices: skinned each frame, refitted
    std::uint32_t rebuilt = 0u;      // of them, built anew this frame
    std::uint32_t ring = 0u;         // draws of this frame's own vertices
    std::uint32_t site = 0u;         // draws of a site's mesh whose vertices name their own matrices
    std::uint32_t flat = 0u;         // no depth test or no colour: sky, volumes
    std::uint32_t sprites = 0u;      // particles, glyphs, widened lines
    std::uint32_t other_view = 0u;   // under another camera or an orthographic view
    std::uint32_t outside = 0u;      // taken, outside the view: the raster passes leave them out (5.3 G-1)
    std::uint32_t repeats = 0u;      // the same mesh under the same matrix again
    std::uint64_t triangles = 0u;    // under the instances that stand still
    std::uint64_t skinned_triangles = 0u;
    std::uint64_t ring_vertices = 0u;
};

// Counts over the lists since the last report.
struct SceneCensus {
    std::uint32_t scenes = 0u;
    SceneCount sum{}, most{};
    std::uint64_t sum_triangles = 0u, sum_skinned_triangles = 0u, sum_ring_vertices = 0u;
    void add(const SceneCount& count) noexcept;
    // One line, then the counts start again.
    void report(std::uint64_t scene_bytes);
};

class Scene {
public:
    explicit Scene(Backend& backend) noexcept : backend_(backend) {}
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    ~Scene() { clear(); }

    // Everything goes now, before the back end stops.
    void clear() noexcept;

    // A frame's instances, in the order added.
    void begin() noexcept;
    void add(const float (*rows)[4], std::uint32_t id, std::uint32_t mask, std::uint64_t structure, Faces faces = Faces::Both,
             std::uint32_t kind = 0u, Micromapped micromapped = Micromapped::None);
    // An instance drawn again by a draw the micromap is not for: micromaps off, its cutout asked at every hit.
    void no_micromap(std::uint32_t instance) noexcept;
    // A draw of an instance already added (the same mesh under the same matrix): its mask joins.
    void also(std::uint32_t instance, std::uint32_t mask) noexcept { instances_[instance].id_mask |= mask << 24u; }
    [[nodiscard]] std::span<const Instance> instances() const noexcept { return instances_; }

    // The structure over instances() as they lie at `address` (the caller copied them there, 16-byte aligned), recorded
    // through the back end's open commands and finished by a barrier. False with no instance or no memory: then no
    // scene this frame.
    [[nodiscard]] bool build(gpu::Address address, std::uint64_t frame_index);
    // What a shader binds, or 0 when this frame has no scene.
    [[nodiscard]] std::uint64_t name() const noexcept { return built_ ? structure_.name : 0u; }
    [[nodiscard]] Structure structure() const noexcept { return built_ ? structure_ : Structure{}; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return structure_bytes_ + scratch_bytes_; }
    [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }

    static constexpr std::uint32_t kLeastCapacity = 256u;

private:
    struct RetiredPage {
        std::uint64_t frame = 0u;
        std::uint32_t page = ~0u;
        Structure structure{};
    };
    void retire(std::uint64_t frame_index);
    void release(std::uint64_t frame_index);

    Backend& backend_;
    std::vector<Instance> instances_;
    std::uint32_t capacity_ = 0u;
    std::uint32_t page_ = ~0u, scratch_page_ = ~0u;
    std::uint64_t structure_bytes_ = 0u, scratch_bytes_ = 0u;
    Structure structure_{};
    std::vector<RetiredPage> retired_;
    bool built_ = false;
};

}  // namespace galaxy::render::rt
