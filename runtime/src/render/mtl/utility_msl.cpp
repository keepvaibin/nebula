// SPDX-License-Identifier: GPL-3.0-only
// The MSL platform/apple/tools/utility_msl.cpp generates at build (utility_msl.h).
#include "galaxy/render/mtl/utility_msl.h"

#include <iterator>

#include "utility_msl_data.h"

namespace galaxy::render::mtl {

namespace {
constexpr UtilityStage Vertex = UtilityStage::Vertex, Fragment = UtilityStage::Fragment, Compute = UtilityStage::Compute;

constexpr UtilityStage kUtilityStages[] = {
#define X(name, source, stage, entry, defines) stage,
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
};

constexpr UtilityStage kPostStages[] = {
#define POST(name, pass, library, quality, stage) stage,
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
};
}  // namespace

static_assert(std::size(kUtilityMsl) == static_cast<unsigned>(UtilityShader::Count),
              "Rebuild the shader tools from this source tree");
static_assert(std::size(kPostMsl) == std::size(kPostStages), "Rebuild the shader tools from this source tree");

const char* utility_msl(UtilityShader shader) noexcept {
    return kUtilityMsl[static_cast<unsigned>(shader)];
}

UtilityStage utility_stage(UtilityShader shader) noexcept {
    return kUtilityStages[static_cast<unsigned>(shader)];
}

const char* post_msl(unsigned program, unsigned smaa_quality) noexcept {
    return kPostMsl[program][smaa_quality];
}

UtilityStage post_stage(unsigned program) noexcept {
    return kPostStages[program];
}

unsigned post_program_count() noexcept {
    return static_cast<unsigned>(std::size(kPostStages));
}

}  // namespace galaxy::render::mtl
