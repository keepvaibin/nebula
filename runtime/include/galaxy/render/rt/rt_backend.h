// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What the tracer asks of a graphics API, one implementation per back end (rt_backend_d3d12.cpp, vk/rt_backend_vk.cpp),
// chosen when the back end is built. Kept small: memory for acceleration structures, their builds, and (later steps)
// a bound scene and compute dispatches. Inline ray queries only, so a back end without ray tracing pipelines can
// provide it. Render thread only.

#include "galaxy/render/gpu_types.h"
#include "galaxy/render/renderer_common.h"

#include <cstdint>
#include <vector>

namespace galaxy::render::rt {

// The driver takes the tracer's shaders: the measuring pipeline is made from this build's bytes and let go. Logged.
// False when the build holds none for this back end or the driver refuses them.
[[nodiscard]] bool shaders_load(gpu::Device* device);

// A triangle mesh a bottom-level structure is built from, in buffers the renderer already holds.
struct TriangleInput {
    gpu::Address positions = 0u;  // three floats at the start of each `position_stride` bytes
    std::uint32_t position_stride = 12u;
    std::uint32_t vertex_count = 0u;
    gpu::Address indices = 0u;  // 0: none, the vertices in threes
    std::uint32_t index_count = 0u;
    std::uint32_t index_bytes = 4u;  // 2 or 4
    [[nodiscard]] std::uint32_t triangles() const noexcept { return (indices != 0u ? index_count : vertex_count) / 3u; }
};

// How a structure is built: once and traced often (compacted afterwards), or refitted as its vertices move.
enum class BuildKind : std::uint8_t { Static, Moving };

struct BuildSizes {
    std::uint64_t structure = 0u;       // the most the structure can take
    std::uint64_t build_scratch = 0u;   // working memory of a build
    std::uint64_t update_scratch = 0u;  // and of a refit (Moving)
};

// A range of a page of structure or scratch memory. `size` 0: none.
struct Range {
    std::uint32_t page = 0u;
    std::uint64_t offset = 0u;
    std::uint64_t size = 0u;
    [[nodiscard]] explicit operator bool() const noexcept { return size != 0u; }
};

// A structure in its range. `name`: what an instance of a top-level structure names it by (its address on the GPU).
struct Structure {
    Range range{};
    std::uint64_t name = 0u;
    std::uint64_t handle = 0u;  // Vulkan: its VkAccelerationStructureKHR; Direct3D 12: none
    [[nodiscard]] explicit operator bool() const noexcept { return name != 0u; }
};

// Ranges are aligned to this in both kinds of page (Direct3D 12's structure alignment; Vulkan's structure offsets are
// multiples of it, and a device whose scratch alignment is larger does not start).
inline constexpr std::uint64_t kRangeAlign = 256u;
// Compacted sizes are asked through this many slots a frame.
inline constexpr std::uint32_t kSizeSlots = 1024u;
// What was last named in a frame's commands may go this many frames later (begin_frame has waited for it by then).
inline constexpr std::uint64_t kFramesToRelease = kFramesInFlight + 1u;

class Backend {
public:
    Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    ~Backend();

    // False when the device cannot build (the caller checked rt_caps first: then it can).
    [[nodiscard]] bool start(gpu::Device* device);
    void stop() noexcept;
    [[nodiscard]] bool started() const noexcept;

    [[nodiscard]] BuildSizes sizes(const TriangleInput& input, BuildKind kind) const;

    // A page of memory for structures, or for scratch; ~0u when the device has none left. A page is freed only when
    // nothing recorded can still read it (the caller waits kFramesToRelease frames).
    [[nodiscard]] std::uint32_t make_page(std::uint64_t bytes, bool scratch);
    void free_page(std::uint32_t page) noexcept;

    // The structure object over a range of a structure page, and its end (the range itself stays the caller's).
    [[nodiscard]] Structure make_structure(const Range& range, bool top_level);
    void free_structure(Structure& structure) noexcept;

    // Recording, into the frame's commands (ID3D12GraphicsCommandList* or VkCommandBuffer, no pass open), in frame slot
    // `frame_slot` (the renderer's): each slot's size answers are kept apart.
    void begin(void* commands, unsigned frame_slot) noexcept;
    // A build into `to` with working memory `scratch`, or with `refit` a refit of `to` in place (Moving structures).
    // Builds between two barriers have ranges of their own in every page.
    void build(const TriangleInput& input, BuildKind kind, const Structure& to, const Range& scratch, bool refit);
    // What was built before is whole for what follows: a refit of it, a copy of it, a structure over it; and the
    // scratch may be used again.
    void barrier();
    // The compacted size of a Static structure built before the last barrier, into slot `slot` (< kSizeSlots).
    void ask_compacted_size(const Structure& structure, std::uint32_t slot);
    // Makes the sizes asked this frame readable: once, after the frame's last ask_compacted_size.
    void end_size_asks();
    // A slot's answer, once the GPU has finished the frame that asked in frame slot `frame_slot` (the renderer waits for
    // it before the slot's next frame) and until that slot asks again.
    [[nodiscard]] std::uint64_t compacted_size(std::uint32_t slot, unsigned frame_slot) const;
    // The compacted copy of `from` into `to` (a range of that size).
    void compact(const Structure& from, const Structure& to);

private:
    struct State;
    State* state_ = nullptr;
};

}  // namespace galaxy::render::rt
