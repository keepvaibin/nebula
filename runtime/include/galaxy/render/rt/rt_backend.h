// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What the tracer asks of a graphics API, one implementation per back end (rt_backend_d3d12.cpp, vk/rt_backend_vk.cpp),
// chosen when the back end is built. Kept small: memory for acceleration structures, their builds, and (later steps)
// a bound scene and compute dispatches. Inline ray queries only, so a back end without ray tracing pipelines can
// provide it. Render thread only.

#include "galaxy/render/gpu_types.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/rt/rt_shaders.h"

#include <cstdint>
#include <vector>

namespace galaxy::render::rt {

// The driver takes the tracer's shaders: the measuring pipeline is made from this build's bytes and let go. Logged.
// False when the build holds none for this back end or the driver refuses them.
[[nodiscard]] bool shaders_load(gpu::Device* device);

inline constexpr std::uint32_t kNoBuffer = ~0u;

// A triangle mesh a bottom-level structure is built from, in buffers the renderer already holds. With `own_buffer`,
// its vertices lie in a buffer of the tracer's (make_buffer: a pass wrote them) and `positions` is their byte there.
struct TriangleInput {
    std::uint32_t own_buffer = kNoBuffer;
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

// A pass: one of the tracer's compute shaders (rt_shaders.h) and what it binds, by the registers of rt_bindings.hlsli.
enum class ImageFormat : std::uint8_t { Rgba8, Rgba16f, Rg16f, R16f, R8, R32f, Rgb10a2 };
inline constexpr std::uint32_t kNoImage = ~0u;
inline constexpr unsigned kPassBuffers = 4u, kPassTextures = 4u, kPassReads = 6u, kPassWrites = 6u;
// Mesh memory a pass reads to know a surface where a ray or a pixel meets it (t15 up): the arenas the scene's meshes
// lie in, then the frame's rows (their matrices).
inline constexpr unsigned kPassMeshArenas = 8u, kPassMeshBuffers = kPassMeshArenas + 1u;
// The counters a pass may add to (u6), read back a frame later: numbers for proofs and diagnostics.
inline constexpr unsigned kCounters = 64u;

// Bytes in a buffer of the renderer's (an upload ring's).
struct BufferView {
    gpu::Address at = 0u;
    std::uint64_t bytes = 0u;
};

// A texture of the renderer's, in a use a compute shader reads. Direct3D 12: its ID3D12Resource and the DXGI_FORMAT to
// read it as. Vulkan: its VkImageView and the VkImageLayout it is in.
struct TextureView {
    void* texture = nullptr;
    std::uint32_t format = 0u;
};

struct PassBinds {
    Structure scene{};                         // t0
    BufferView buffers[kPassBuffers]{};        // t1-t4
    TextureView textures[kPassTextures]{};     // t5-t8
    std::uint32_t reads[kPassReads] = {kNoImage, kNoImage, kNoImage, kNoImage, kNoImage, kNoImage};  // t9-t14: images of the tracer's
    BufferView mesh[kPassMeshBuffers]{};       // t15-t23: mesh arenas, then the rows
    std::uint32_t writes[kPassWrites] = {kNoImage, kNoImage, kNoImage, kNoImage, kNoImage, kNoImage};  // u0-u5
    BufferView constants{};                    // b0: 256-byte aligned, in an upload ring
    bool counters = false;                     // u6
    std::uint32_t write_buffer = kNoBuffer;    // u7: a buffer of the tracer's, whole (make_buffer)
};

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

    // Once a frame, before its first begin: frame slot `frame_slot` (the renderer's) comes round, and the GPU has
    // finished that slot's last frame (what a pass bound there may go).
    void frame(unsigned frame_slot) noexcept;
    // Vulkan: what `descriptors` of begin points at, the renderer's textures as the draws' programs see them (its
    // image array's set and that set's layout). The passes read them as set 1.
    struct TextureTable {
        void* set = nullptr;
        void* layout = nullptr;
    };
    // Recording, into the frame's commands (ID3D12GraphicsCommandList* or VkCommandBuffer, no pass open), in frame slot
    // `frame_slot`: each slot's size answers are kept apart. `descriptors`: where a pass's views go this frame on
    // Direct3D 12 (the renderer's DescriptorRing*; null when only builds follow); Vulkan keeps pools of its own.
    void begin(void* commands, unsigned frame_slot, void* descriptors = nullptr) noexcept;
    // A build into `to` with working memory `scratch`, or with `refit` a refit of `to` in place (Moving structures).
    // Builds between two barriers have ranges of their own in every page.
    void build(const TriangleInput& input, BuildKind kind, const Structure& to, const Range& scratch, bool refit);
    // The top-level structure: its sizes for up to `instances` instances, and its build into `to` (made with
    // `top_level`) from `count` instances lying at `instances` in a buffer of the renderer's (64 bytes each, the layout
    // both APIs share), every structure they name whole.
    [[nodiscard]] BuildSizes scene_sizes(std::uint32_t instances) const;
    void build_scene(gpu::Address instances, std::uint32_t count, const Structure& to, const Range& scratch);
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

    // An image of the tracer's: written by one pass, read by the next. kNoImage when the device has no memory. Freed
    // only when nothing recorded can still read it (the caller waits kFramesToRelease frames).
    [[nodiscard]] std::uint32_t make_image(ImageFormat format, std::uint32_t width, std::uint32_t height, const char* name);
    void free_image(std::uint32_t image) noexcept;
    // The pass, `groups_x` by `groups_y` groups. Nothing when the shader is not in this build or the driver refused it
    // (told once).
    void dispatch(RtShader shader, const PassBinds& binds, std::uint32_t groups_x, std::uint32_t groups_y);
    // A buffer of the tracer's that passes write and builds read (skinned vertices). kNoBuffer when the device has no
    // memory. `buffer_for` puts it in the use its next commands have for it: written by passes, or read by builds.
    [[nodiscard]] std::uint32_t make_buffer(std::uint64_t bytes, const char* name);
    void free_buffer(std::uint32_t buffer) noexcept;
    void buffer_for(std::uint32_t buffer, bool builds);
    // What a pass binds to read `bytes` at `at` in a buffer of the renderer's: a view starting where the API lets one
    // start, and where `at` is in it (`offset`, bytes).
    [[nodiscard]] BufferView view_of(gpu::Address at, std::uint64_t bytes, std::uint32_t& offset) const;
    // The image as the renderer's own texture (ID3D12Resource*; vk::Texture*), put in copy-source use: the renderer
    // copies from it in these commands. The next pass that binds it changes its use again.
    [[nodiscard]] void* image_to_copy(std::uint32_t image);
    // The image as a texture of the renderer's that the draws' programs sample (the traced light), put in that use.
    [[nodiscard]] void* image_to_sample(std::uint32_t image);
    // The counters: zeroed before the frame's first pass that binds them; `end_counters` makes what was added so far
    // readable (after each pass that counts: the frame's last call holds them all). `read_counters`: the values of
    // the frame slot's last finished frame (zero before one).
    void end_counters();
    void read_counters(unsigned frame_slot, std::uint32_t out[kCounters]) const;

private:
    struct State;
    State* state_ = nullptr;
};

}  // namespace galaxy::render::rt
