// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// draw.post's host side (LumaCore plan 2.4 layer 8 and 3.4 layer 4; contract draw 15): a mod's post programs.
// The game registers a program's text (G's cook: the mod's `float4 post(PostIn i)` and its parameters mapped onto
// post_params) through NebulaHostServices::post_register; the host puts its prelude before it (PostIn, scene,
// scene_texel, depth, motion, the bindings of the backend that draws) and its entry point after, and compiles it with
// the compiler the running renderer installed (D3D12: Microsoft's dxc beside the game, as mod material programs;
// Vulkan: the linked HLSL front end). A NEBULA_PASS_POST in the frame list then draws it full screen into a new
// picture that is the frame from there (RenderCore::walk_post).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::render {

// post_register's `reads` (contract draw 15 "reads").
enum : std::uint32_t { kModPostReadsDepth = 1u, kModPostReadsMotion = 2u, kModPostReadsAll = 3u };
// ModPostConstants::flags: what the frame has for the inputs read (a missing one reads as far / no motion).
enum : std::uint32_t { kModPostHasDepth = 1u, kModPostHasMotion = 2u };
// The constant block, 8 rows of 16 bytes: D3D12 root constants at b0, Vulkan push constants (128 bytes, the least
// every Vulkan device gives).
struct ModPostConstants {
    float size[2];          // the frame's pixels
    float inv_size[2];
    float time;             // seconds, the materials' time
    std::uint32_t frame;    // the game frame that made the list
    std::uint32_t flags;    // kModPostHas*
    float reserved0;
    float depth_terms[4];   // distance along the view axis = x / (d * y + z), d the depth target's value; w the far plane's
    float motion_scale[2];  // the motion target's value times this = where the surface was, minus uv (uv units)
    float reserved1[2];
    float params[16];       // the program's parameters, 4 rows
};
static_assert(sizeof(ModPostConstants) == 128u);
inline constexpr std::uint32_t kModPostConstantWords = sizeof(ModPostConstants) / 4u;

// The binding layout both backends draw with: frame t0, depth t1, motion t2, linear clamp s0, point clamp s1, the
// block b0. On Vulkan set 0: the frame binding 0, depth 1, motion 2, the samplers 3 and 4, the block push constants.
struct ModPostProgram {
    std::uint32_t id = 0u;
    std::uint32_t reads = 0u;
    std::vector<std::uint8_t> vertex;  // the host's full-screen triangle, compiled once
    std::vector<std::uint8_t> pixel;
};

// One stage for the running backend; empty, with the compiler's first error in `problem`, when it cannot be. `work`:
// a folder the compiler may write its scratch files in (set_mod_post_work_folder).
using ModPostCompiler = std::vector<std::uint8_t> (*)(const std::string& source, const std::filesystem::path& work,
                                                      bool pixel, std::string& problem);
// The running backend's compiler, beside the mod material compiler of the same build (mod_shader_compile_d3d12.cpp,
// vk/mod_shader_compile_vk.cpp): what the renderer installs.
std::vector<std::uint8_t> compile_mod_post_stage(const std::string& source, const std::filesystem::path& work, bool pixel,
                                                 std::string& problem);
// The renderer that draws installs its compiler when it starts (nullptr when it stops); with none every program is
// refused ("no compiler for post programs on this renderer").
void set_mod_post_compiler(ModPostCompiler compiler, bool spirv) noexcept;
// Where compiles write scratch files: the host's cooked-mods folder for the session (host_main), so a test run keeps
// to its work folder. Unset, the system's temporary folder.
void set_mod_post_work_folder(const std::filesystem::path& folder);
// Whether the frame can give motion (temporal upscaling on), set by the renderer once a frame: a program reading
// motion is refused without it.
void set_mod_post_motion(bool available) noexcept;

// The full program for the running backend: the prelude, `text`, the entry point.
[[nodiscard]] std::string mod_post_source(std::string_view text, bool spirv);
// post_register's results (nebula_game_api.h NEBULA_POST_*).
enum : std::int32_t { kModPostOk = 0, kModPostCompile = 1, kModPostNoMotion = 2 };
// post_register: kModPostOk and *program (never 0); else *program 0 and the first error (cut to `capacity`, ended):
// kModPostNoMotion for a program reading motion where the frame has none (refusal 542), kModPostCompile for the rest.
// Any thread; compiles before it returns.
std::int32_t mod_post_register(const void* text, std::uint32_t size, std::uint32_t reads, std::uint32_t* program, char* error,
                               std::uint32_t capacity);
// post_free: the program leaves; a list that still names it draws nothing for it (its pass is skipped).
void mod_post_free(std::uint32_t program);
// The program a pass names, null when unknown or freed. The renderer keeps what it builds from it by id and drops it
// when this answers null (mod_post_epoch changes whenever a program leaves).
[[nodiscard]] std::shared_ptr<const ModPostProgram> mod_post_program(std::uint32_t program);
[[nodiscard]] std::uint64_t mod_post_epoch() noexcept;

}  // namespace galaxy::render
