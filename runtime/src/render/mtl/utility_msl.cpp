// SPDX-License-Identifier: GPL-3.0-only
// The MSL platform/apple/tools/utility_msl.cpp generates at build (utility_msl.h).
#include "galaxy/render/mtl/utility_msl.h"

#include <algorithm>
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

std::vector<const char*> renderer_msl_texts() {
    std::vector<const char*> texts;
    const auto add = [&texts](const char* text) {
        if (std::find(texts.begin(), texts.end(), text) == texts.end()) {
            texts.push_back(text);
        }
    };
    for (unsigned shader = 0u; shader < static_cast<unsigned>(UtilityShader::Count); ++shader) {
        if (metal_uses(static_cast<UtilityShader>(shader))) {
            add(kUtilityMsl[shader]);
        }
    }
    // A program that does not follow the SMAA quality names its one text at every quality.
    for (unsigned program = 0u; program < post_program_count(); ++program) {
        if (metal_uses_post(program)) {
            for (unsigned quality = 0u; quality < 4u; ++quality) {
                add(kPostMsl[program][quality]);
            }
        }
    }
    add(kDepthCopyVsMsl);
    add(kDepthCopyPsMsl);
    return texts;
}

}  // namespace galaxy::render::mtl
