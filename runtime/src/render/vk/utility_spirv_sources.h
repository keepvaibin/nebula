// SPDX-License-Identifier: GPL-3.0-only
// What tools/dev/utility_spirv.cpp compiles besides the shared sources: the Vulkan-only shaders, the binding of every
// D3D12 register (annotate()) and the composition of the presentation passes.
#pragma once

#include "galaxy/render/vk/shader_compile_vk.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace galaxy::render {
// The shader sources cmake/post_shader_sources.cmake embeds.
extern const unsigned char galaxy_post_post_process_hlsl[];
extern const unsigned char galaxy_post_smaa_hlsl[];
extern const unsigned char galaxy_post_ffx_a_h[];
extern const unsigned char galaxy_post_ffx_fsr1_h[];
extern const unsigned char galaxy_post_game_bloom_hlsl[];
extern const unsigned char galaxy_post_ambient_occlusion_hlsl[];
extern const unsigned char galaxy_post_scene_camera_hlsl[];
}  // namespace galaxy::render

namespace galaxy::render::vk {

// The binding layouts of the renderer's own shaders.
constexpr RegisterBinding kUtilityBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}};
constexpr RegisterBinding kConversionBindings[] = {
    {"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}, {"s1", 0, 2}};

// The display picture into the swap chain image (the present pass's blit), turned for a pre-rotated image: `transform` is the
// image's VkSurfaceTransformFlagBitsKHR (identity, 90, 180 or 270 degrees clockwise). t is the position in the
// viewport, which already covers the picture's turned rectangle; uv is the picture's point shown there.
constexpr char kPresentVs[] = R"HLSL(
cbuffer C : register(b0) { uint transform; };
struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };
V main(uint id : SV_VertexID) {
    V o;
    float2 t = float2(id == 1 ? 2 : 0, id == 2 ? 2 : 0);
    o.pos = float4(t.x * 2 - 1, 1 - t.y * 2, 0.5, 1);
    o.uv = transform == 2u ? float2(t.y, 1 - t.x)
         : transform == 4u ? float2(1 - t.x, 1 - t.y)
         : transform == 8u ? float2(1 - t.y, t.x)
         : t;
    return o;
}
)HLSL";

// The depth resolve: each pixel's sample 0.
constexpr char kDepthResolvePs[] = R"HLSL(
Texture2DMS<float> t0 : register(t0);
float main(float4 pos : SV_Position) : SV_Depth {
    return t0.Load(int2(pos.xy), 0);
}
)HLSL";

// post_process.hlsl's registers: textures t0-t2 in set 0, the linear and point samplers in set 1, the constants
// pushed.
constexpr RegisterBinding kPostBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"t1", 0, 1},
                                         {"t2", 0, 2},       {"s0", 1, 0}, {"s1", 1, 1}};

enum class Library { kNone, kSmaa, kFsr };

inline const char* text_of(const unsigned char* bytes) {
    return reinterpret_cast<const char*>(bytes);
}

// One pass's source, composed as CMakeLists.txt's galaxy_post_shader composes the D3D12 build's fxc inputs.
inline std::string compose(const char* pass, const char* preset, Library library) {
    std::string text = std::string("#define ") + pass + " 1\n";
    if (preset != nullptr) {
        text += std::string("#define ") + preset + " 1\n";
    }
    text += "#define POST_SECTION 1\n";
    text += text_of(galaxy_post_post_process_hlsl);
    text += '\n';
    if (library == Library::kSmaa) {
        text += text_of(galaxy_post_smaa_hlsl);
        text += '\n';
    } else if (library == Library::kFsr) {
        text += text_of(galaxy_post_ffx_a_h);
        text += "\n#undef POST_SECTION\n#define POST_SECTION 2\n";
        text += text_of(galaxy_post_post_process_hlsl);
        text += '\n';
        text += text_of(galaxy_post_ffx_fsr1_h);
        text += '\n';
    }
    text += "#undef POST_SECTION\n#define POST_SECTION 3\n";
    text += text_of(galaxy_post_post_process_hlsl);
    text += '\n';
    return text;
}

inline const char* smaa_preset(unsigned quality) {
    static constexpr const char* kPresets[4] = {"SMAA_PRESET_LOW", "SMAA_PRESET_MEDIUM", "SMAA_PRESET_HIGH",
                                                "SMAA_PRESET_ULTRA"};
    return kPresets[std::min(quality, 3u)];
}

// game_bloom.hlsl's registers: t0-t2 sampled and u0-u2 storage images in set 0, s0 in set 1, the constants pushed.
constexpr RegisterBinding kBloomBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"t1", 0, 1}, {"t2", 0, 2},
                                         {"u0", 0, 3},       {"u1", 0, 4}, {"u2", 0, 5}, {"s0", 1, 0}};

// ambient_occlusion.hlsl's registers: t0-t2 sampled, u0 a storage image and t3-t4 storage buffers in set 0, the constants pushed.
constexpr RegisterBinding kAoBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"t1", 0, 1}, {"t2", 0, 2},
                                           {"u0", 0, 3},       {"t3", 0, 4}, {"t4", 0, 5}};

inline std::string ao_source() {
    return "#define AO_STORAGE_FORMAT 1\n" + annotate(reinterpret_cast<const char*>(galaxy_post_ambient_occlusion_hlsl), kAoBindings);
}

// scene_camera.hlsl's registers: t0-t1 sampled and u0 a storage image in set 0, the constants pushed.
constexpr RegisterBinding kCameraBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"t1", 0, 1}, {"u0", 0, 2}, {"t2", 0, 3}};

inline std::string camera_source() {
    return "#define CAMERA_STORAGE_FORMAT 1\n" + annotate(reinterpret_cast<const char*>(galaxy_post_scene_camera_hlsl), kCameraBindings);
}

inline std::string bloom_source() {
    return annotate(reinterpret_cast<const char*>(galaxy_post_game_bloom_hlsl), kBloomBindings);
}

}  // namespace galaxy::render::vk
