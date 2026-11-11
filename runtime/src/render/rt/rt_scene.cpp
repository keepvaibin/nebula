// SPDX-License-Identifier: GPL-3.0-only
#include "render/rt/rt_scene.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

namespace galaxy::render::rt {

namespace {

constexpr std::uint64_t aligned(std::uint64_t bytes) noexcept { return (bytes + kRangeAlign - 1u) & ~(kRangeAlign - 1u); }

}  // namespace

void SceneCensus::add(const SceneCount& count) noexcept {
    ++scenes;
    const auto each = [&](std::uint32_t SceneCount::* field) {
        sum.*field += count.*field;
        most.*field = std::max(most.*field, count.*field);
    };
    each(&SceneCount::instances);
    each(&SceneCount::opaque);
    each(&SceneCount::blended);
    each(&SceneCount::waiting);
    each(&SceneCount::skinned);
    each(&SceneCount::rebuilt);
    each(&SceneCount::ring);
    each(&SceneCount::site);
    each(&SceneCount::flat);
    each(&SceneCount::sprites);
    each(&SceneCount::other_view);
    each(&SceneCount::outside);
    each(&SceneCount::repeats);
    sum_triangles += count.triangles;
    sum_skinned_triangles += count.skinned_triangles;
    sum_ring_vertices += count.ring_vertices;
    most.triangles = std::max(most.triangles, count.triangles);
    most.skinned_triangles = std::max(most.skinned_triangles, count.skinned_triangles);
    most.ring_vertices = std::max(most.ring_vertices, count.ring_vertices);
}

void SceneCensus::report(std::uint64_t scene_bytes) {
    if (scenes == 0u) {
        return;
    }
    const double n = static_cast<double>(scenes);
    const auto mean = [&](std::uint32_t SceneCount::* field) { return static_cast<double>(sum.*field) / n; };
    std::fprintf(stderr,
                 "[rt-inputs] scene over %u frames, mean (most): instances %.1f (%u) opaque %.1f (%u) blended %.1f (%u) triangles %.0f (%llu) "
                 "waiting %.2f (%u) repeats %.1f (%u) skinned %.1f (%u) of %.0f (%llu) triangles, rebuilt %.2f (%u); left out: ring %.1f "
                 "(%u) of %.0f (%llu) vertices, site %.1f (%u), flat %.1f (%u), sprites %.1f (%u), other-view %.1f (%u); outside-view %.1f (%u); "
                 "memory %.2f MB\n",
                 scenes, mean(&SceneCount::instances), most.instances, mean(&SceneCount::opaque), most.opaque, mean(&SceneCount::blended),
                 most.blended, static_cast<double>(sum_triangles) / n, static_cast<unsigned long long>(most.triangles),
                 mean(&SceneCount::waiting), most.waiting, mean(&SceneCount::repeats), most.repeats, mean(&SceneCount::skinned), most.skinned,
                 static_cast<double>(sum_skinned_triangles) / n, static_cast<unsigned long long>(most.skinned_triangles),
                 mean(&SceneCount::rebuilt), most.rebuilt, mean(&SceneCount::ring), most.ring, static_cast<double>(sum_ring_vertices) / n,
                 static_cast<unsigned long long>(most.ring_vertices), mean(&SceneCount::site), most.site, mean(&SceneCount::flat), most.flat,
                 mean(&SceneCount::sprites), most.sprites, mean(&SceneCount::other_view), most.other_view,
                 mean(&SceneCount::outside), most.outside, static_cast<double>(scene_bytes) / 1048576.0);
    *this = SceneCensus{};
}

void Scene::clear() noexcept {
    for (RetiredPage& retired : retired_) {
        backend_.free_structure(retired.structure);
        backend_.free_page(retired.page);
    }
    retired_.clear();
    backend_.free_structure(structure_);
    if (page_ != ~0u) {
        backend_.free_page(page_);
    }
    if (scratch_page_ != ~0u) {
        backend_.free_page(scratch_page_);
    }
    page_ = scratch_page_ = ~0u;
    structure_bytes_ = scratch_bytes_ = 0u;
    capacity_ = 0u;
    instances_.clear();
    built_ = false;
}

void Scene::begin() noexcept {
    instances_.clear();
    built_ = false;
}

// The draws' front face is the one whose vertices run clockwise on screen; a ray's is the one whose normal (the cross
// product of its edges, in the mesh's own space) points at the ray's origin. Through an instance's matrix A the two
// agree when det A < 0 (the eye's space is right-handed), so an instance flips its facing when det A > 0; a draw that
// shows its back faces flips once more.
void Scene::add(const float (*rows)[4], std::uint32_t id, std::uint32_t mask, std::uint64_t structure, Faces faces,
                std::uint32_t kind, Micromapped micromapped) {
    Instance& instance = instances_.emplace_back();
    std::memcpy(instance.transform, rows, sizeof(instance.transform));
    instance.id_mask = (id & 0xFFFFFFu) | mask << 24u;
    std::uint32_t flags = kInstanceNoCull;
    if (faces != Faces::Both) {
        const float (*m)[4] = instance.transform;
        const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                          m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        flags = (det > 0.0f) == (faces == Faces::Front) ? kInstanceFlip : 0u;
    }
    if ((kind & kKindCutout) != 0u && micromapped != Micromapped::Own) {
        flags |= kInstanceNotOpaque;  // a ray that does not force hits opaque reads its alpha at each one
    }
    if (micromapped == Micromapped::Other) {
        flags |= kInstanceNoMicromaps;
    }
    instance.flags = flags << 24u | (kind & 0xFFFFFFu);
    instance.structure = structure;
}

void Scene::no_micromap(std::uint32_t instance) noexcept {
    Instance& at = instances_[instance];
    at.flags |= (kInstanceNoMicromaps | ((at.flags & kKindCutout) != 0u ? kInstanceNotOpaque : 0u)) << 24u;
}

// The structure and its scratch in use go once no recorded frame can read them.
void Scene::retire(std::uint64_t frame_index) {
    if (page_ != ~0u) {
        retired_.push_back({frame_index, page_, structure_});
    }
    if (scratch_page_ != ~0u) {
        retired_.push_back({frame_index, scratch_page_, Structure{}});
    }
    page_ = scratch_page_ = ~0u;
    structure_ = Structure{};
    structure_bytes_ = scratch_bytes_ = 0u;
    capacity_ = 0u;
}

void Scene::release(std::uint64_t frame_index) {
    std::erase_if(retired_, [&](RetiredPage& retired) {
        if (retired.frame + kFramesToRelease > frame_index) {
            return false;
        }
        backend_.free_structure(retired.structure);
        backend_.free_page(retired.page);
        return true;
    });
}

bool Scene::build(gpu::Address address, std::uint64_t frame_index) {
    release(frame_index);
    built_ = false;
    const auto count = static_cast<std::uint32_t>(instances_.size());
    if (count == 0u || address == 0u) {
        return false;
    }
    if (count > capacity_) {
        // Room for the next scenes too: the next power of two.
        retire(frame_index);
        const std::uint32_t capacity = std::max(kLeastCapacity, std::bit_ceil(count));
        const BuildSizes sizes = backend_.scene_sizes(capacity);
        const std::uint64_t structure_bytes = aligned(sizes.structure), scratch_bytes = aligned(sizes.build_scratch);
        page_ = backend_.make_page(structure_bytes, false);
        scratch_page_ = backend_.make_page(scratch_bytes, true);
        if (page_ != ~0u && scratch_page_ != ~0u) {
            structure_ = backend_.make_structure({page_, 0u, structure_bytes}, true);
        }
        if (!structure_) {
            if (page_ != ~0u) {
                backend_.free_page(page_);
            }
            if (scratch_page_ != ~0u) {
                backend_.free_page(scratch_page_);
            }
            page_ = scratch_page_ = ~0u;
            return false;
        }
        structure_bytes_ = structure_bytes;
        scratch_bytes_ = scratch_bytes;
        capacity_ = capacity;
    }
    backend_.build_scene(address, count, structure_, {scratch_page_, 0u, scratch_bytes_});
    backend_.barrier();
    built_ = true;
    return true;
}

}  // namespace galaxy::render::rt
