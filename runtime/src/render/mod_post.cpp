// SPDX-License-Identifier: GPL-3.0-only
// draw.post's host side: the registry of mods' post programs, their prelude and entry point (mod_post.h).
#include "galaxy/render/mod_post.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace galaxy::render {
namespace {

// Before the mod's text. The cooked text (G, 3.4 layer 4) uses PostIn, scene, scene_texel, depth, motion and
// post_params (its parameters are #defines onto it) and defines `float4 post(PostIn i)`; it declares no binding.
constexpr const char* kPrelude = R"(// draw.post prelude (host; contract draw 15)
struct PostIn {
    float2 uv;    // 0 to 1, top left 0, at the pixel's centre
    float2 pixel; // pixels, at the pixel's centre
    float2 size;  // the frame's pixels
    float time;   // seconds, the materials' time
};
#if defined(NEBULA_POST_SPIRV)
[[vk::push_constant]] cbuffer NebulaPostBlock {
#else
cbuffer NebulaPostBlock : register(b0) {
#endif
    float2 nebula_post_size;
    float2 nebula_post_inv_size;
    float nebula_post_time;
    uint nebula_post_frame;
    uint nebula_post_flags;
    float nebula_post_reserved0;
    float4 nebula_post_depth_terms;
    float2 nebula_post_motion_scale;
    float2 nebula_post_reserved1;
    float4 post_params[4];
};
#if defined(NEBULA_POST_SPIRV)
[[vk::binding(0, 0)]] Texture2D<float4> nebula_post_frame_in;
[[vk::binding(1, 0)]] Texture2D<float> nebula_post_depth_in;
[[vk::binding(2, 0)]] Texture2D<float2> nebula_post_motion_in;
[[vk::binding(3, 0)]] SamplerState nebula_post_linear;
[[vk::binding(4, 0)]] SamplerState nebula_post_point;
#else
Texture2D<float4> nebula_post_frame_in : register(t0);
Texture2D<float> nebula_post_depth_in : register(t1);
Texture2D<float2> nebula_post_motion_in : register(t2);
SamplerState nebula_post_linear : register(s0);
SamplerState nebula_post_point : register(s1);
#endif
// The frame so far (display-encoded, 0 to 1), with the stage's posts before this one applied.
float4 scene(float2 uv) {
    return nebula_post_frame_in.SampleLevel(nebula_post_linear, uv, 0.0);
}
float4 scene_texel(int2 pixel) {
    const int2 last = int2(nebula_post_size) - int2(1, 1);
    return nebula_post_frame_in.Load(int3(clamp(pixel, int2(0, 0), last), 0));
}
// The distance from the eye along the view axis, game units; the far plane's where nothing was drawn.
float depth(float2 uv) {
    const float far_distance = nebula_post_depth_terms.w;
    if ((nebula_post_flags & 1u) == 0u) {
        return far_distance;
    }
    const float d = nebula_post_depth_in.SampleLevel(nebula_post_point, uv, 0.0);
    const float below = d * nebula_post_depth_terms.y + nebula_post_depth_terms.z;
    return below > 0.0 ? min(nebula_post_depth_terms.x / below, far_distance) : far_distance;
}
// Where that surface was the frame before, minus uv (uv units); 0 where the frame has no motion.
float2 motion(float2 uv) {
    if ((nebula_post_flags & 2u) == 0u) {
        return float2(0.0, 0.0);
    }
    return nebula_post_motion_in.SampleLevel(nebula_post_point, uv, 0.0) * nebula_post_motion_scale;
}
#line 1
)";

// After it: the host's entry points. The triangle covers the target whatever the backend's y; uv comes from the
// pixel's position, so it is top-left based on both.
constexpr const char* kEntry = R"(
float4 program_vs(uint id : SV_VertexID) : SV_Position {
    const float2 corner = float2((id << 1u) & 2u, id & 2u);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}
float4 program_ps(float4 position : SV_Position) : SV_Target0 {
    PostIn i;
    i.pixel = position.xy;
    i.size = nebula_post_size;
    i.uv = position.xy * nebula_post_inv_size;
    i.time = nebula_post_time;
    return float4(post(i).rgb, 1.0);
}
)";

struct Registry {
    std::mutex mutex;
    ModPostCompiler compiler = nullptr;
    bool spirv = false;
    std::filesystem::path work;
    std::vector<std::uint8_t> vertex;  // compiled once per compiler
    std::uint32_t next = 1u;
    std::unordered_map<std::uint32_t, std::shared_ptr<const ModPostProgram>> programs;
};

Registry& registry() {
    static Registry r;
    return r;
}

std::atomic<std::uint64_t> g_epoch{0u};
// Whether the frame can give motion: the renderer sets it once a frame, a registration reads it.
std::atomic<bool> g_motion{false};

std::int32_t refuse(char* error, std::uint32_t capacity, const std::string& why, std::int32_t result = kModPostCompile) {
    if (error != nullptr && capacity != 0u) {
        const std::size_t n = std::min<std::size_t>(why.size(), capacity - 1u);
        std::memcpy(error, why.data(), n);
        error[n] = '\0';
    }
    return result;
}

// The first line of a compiler's report that names an error (the whole first line when none does).
std::string first_error(const std::string& problem) {
    std::size_t start = 0u;
    while (start < problem.size()) {
        std::size_t end = problem.find('\n', start);
        if (end == std::string::npos) {
            end = problem.size();
        }
        const std::string line = problem.substr(start, end - start);
        if (line.find("error") != std::string::npos) {
            return line;
        }
        start = end + 1u;
    }
    return problem.substr(0u, problem.find('\n'));
}

}  // namespace

void set_mod_post_compiler(ModPostCompiler compiler, bool spirv) noexcept {
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    if (r.compiler != compiler || r.spirv != spirv) {
        r.vertex.clear();
    }
    r.compiler = compiler;
    r.spirv = spirv;
}

void set_mod_post_motion(bool available) noexcept {
    g_motion.store(available, std::memory_order_relaxed);
}

void set_mod_post_work_folder(const std::filesystem::path& folder) {
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    r.work = folder;
}

std::string mod_post_source(std::string_view text, bool spirv) {
    std::string source;
    source.reserve(text.size() + 4096u);
    if (spirv) {
        source += "#define NEBULA_POST_SPIRV 1\n";
    }
    source += kPrelude;
    source.append(text.data(), text.size());
    source += kEntry;
    return source;
}

std::int32_t mod_post_register(const void* text, std::uint32_t size, std::uint32_t reads, std::uint32_t* out, char* error,
                               std::uint32_t capacity) {
    if (out == nullptr) {
        return refuse(error, capacity, "no place for the program's id");
    }
    *out = 0u;
    if (text == nullptr || size == 0u) {
        return refuse(error, capacity, "the program has no text");
    }
    if ((reads & ~kModPostReadsAll) != 0u) {
        return refuse(error, capacity, "reads has a bit the host does not know");
    }
    if ((reads & kModPostReadsMotion) != 0u && !g_motion.load(std::memory_order_relaxed)) {
        return refuse(error, capacity, "no motion: the frame has no motion vectors (temporal upscaling is off)",
                      kModPostNoMotion);
    }
    ModPostCompiler compiler;
    bool spirv;
    std::filesystem::path work;
    {
        Registry& r = registry();
        const std::lock_guard<std::mutex> lock(r.mutex);
        compiler = r.compiler;
        spirv = r.spirv;
        work = r.work;
    }
    if (compiler == nullptr) {
        return refuse(error, capacity, "no compiler for post programs on this renderer");
    }
    {
        std::error_code ignored;
        if (work.empty()) {
            work = std::filesystem::temp_directory_path(ignored);
        }
        // The cooked-mods folder exists only once a mod's files were cooked.
        std::filesystem::create_directories(work, ignored);
    }
    auto program = std::make_shared<ModPostProgram>();
    program->reads = reads;
    // The text is the mod's: a NUL inside it would end the compiler's read early, so it is refused.
    const std::string_view body(static_cast<const char*>(text), size);
    if (body.find('\0') != std::string_view::npos) {
        return refuse(error, capacity, "the program's text holds a NUL");
    }
    const std::string source = mod_post_source(body, spirv);
    std::string problem;
    program->pixel = compiler(source, work, true, problem);
    if (program->pixel.empty()) {
        return refuse(error, capacity, first_error(problem.empty() ? std::string("the compiler gave nothing") : problem));
    }
    Registry& r = registry();
    std::vector<std::uint8_t> vertex;
    {
        const std::lock_guard<std::mutex> lock(r.mutex);
        vertex = r.vertex;
    }
    if (vertex.empty()) {
        // The vertex stage is the host's own and the same for every program: compiled with the first.
        problem.clear();
        vertex = compiler(source, work, false, problem);
        if (vertex.empty()) {
            return refuse(error, capacity, "the host's vertex stage: " + first_error(problem));
        }
    }
    program->vertex = vertex;
    const std::lock_guard<std::mutex> lock(r.mutex);
    if (r.compiler != compiler) {
        return refuse(error, capacity, "the renderer changed while the program compiled");
    }
    if (r.vertex.empty()) {
        r.vertex = vertex;
    }
    // Ids are never reused in a session, so a list naming a freed program can never draw another one.
    if (r.next == 0u) {
        return refuse(error, capacity, "no program id is left");
    }
    program->id = r.next++;
    r.programs.emplace(program->id, program);
    *out = program->id;
    return kModPostOk;
}

void mod_post_free(std::uint32_t program) {
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    if (r.programs.erase(program) != 0u) {
        g_epoch.fetch_add(1u, std::memory_order_release);
    }
}

std::shared_ptr<const ModPostProgram> mod_post_program(std::uint32_t program) {
    Registry& r = registry();
    const std::lock_guard<std::mutex> lock(r.mutex);
    const auto it = r.programs.find(program);
    return it == r.programs.end() ? nullptr : it->second;
}

std::uint64_t mod_post_epoch() noexcept {
    return g_epoch.load(std::memory_order_acquire);
}

}  // namespace galaxy::render
