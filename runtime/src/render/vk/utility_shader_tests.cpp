// SPDX-License-Identifier: GPL-3.0-only
// The renderer's shared utility shaders (renderer_common.h) through the Vulkan HLSL front end: each compiles, as
// RendererVk::create_utility_pipelines compiles them at start (glslang reserves words D3D12's compiler accepts).
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/vk/shader_compile_vk.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

namespace vk = galaxy::render::vk;
namespace render = galaxy::render;

// RendererVk's bindings for these shaders (renderer_vk.cpp kUtilityBindings, kConversionBindings).
constexpr vk::RegisterBinding kBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}, {"s1", 0, 2}};

bool compiles(const char* name, std::string_view hlsl, vk::ShaderStage stage) {
    std::string error;
    const auto spirv = vk::compile_hlsl(vk::annotate(hlsl, kBindings), stage, "main", {}, error);
    if (spirv.empty()) {
        std::fprintf(stderr, "FAIL %s: %s\n", name, error.c_str());
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using S = vk::ShaderStage;
    bool passed = true;
    passed &= compiles("efb_conversion", render::efb_conversion_shader_source(), S::Fragment);
    passed &= compiles("display_filter", render::display_filter_shader_source(), S::Fragment);
    passed &= compiles("blit_vertex", render::blit_vertex_shader_source(), S::Vertex);
    passed &= compiles("blit_pixel", render::blit_pixel_shader_source(), S::Fragment);
    passed &= compiles("clear_pixel", render::clear_pixel_shader_source(), S::Fragment);
    passed &= compiles("clear_depth_pixel", render::clear_depth_pixel_shader_source(), S::Fragment);
    passed &= compiles("depth_carry_load_pixel", render::depth_carry_load_pixel_shader_source(), S::Fragment);
    passed &= compiles("depth_carry_pixel", render::depth_carry_pixel_shader_source(), S::Fragment);
    passed &= compiles("ui_textured_quad_vertex", render::ui_textured_quad_vertex_shader_source(), S::Vertex);
    passed &= compiles("ui_textured_quad_pixel", render::ui_textured_quad_pixel_shader_source(), S::Fragment);
    passed &= compiles("ui_solid_quad_vertex", render::ui_solid_quad_vertex_shader_source(), S::Vertex);
    passed &= compiles("ui_solid_quad_pixel", render::ui_solid_quad_pixel_shader_source(), S::Fragment);
    std::printf("%s\n", passed ? "vk utility shaders: pass" : "vk utility shaders: FAIL");
    return passed ? 0 : 1;
}
