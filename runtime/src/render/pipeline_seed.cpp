// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/pipeline_seed.h"
#include "galaxy/test_hooks.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <tuple>

namespace galaxy::render {

namespace {
constexpr PipelineSeed kSeeds[] = {
#include "pipeline_seeds.inc"  // the same on both backends (tools/dev/pipeline_seed.py)
    {0u, 0u, 0u, 0u, 0u, 0u},
};
}  // namespace

std::span<const PipelineSeed> pipeline_seeds() noexcept {
    return std::span<const PipelineSeed>(kSeeds, std::size(kSeeds) - 1u);
}

void note_pipeline_for_seed(std::uint32_t shader, const RenderStateKey& rs) noexcept {
    static std::FILE* const file = [] {
        std::size_t length = 0;
        char path[512] = {};
        std::FILE* out = nullptr;
        if (getenv_s(&length, path, sizeof(path), nebula_switch::pipeline_seed_out_name()) == 0 && length > 1u) {
            (void)fopen_s(&out, path, "ab");
        }
        return out;
    }();
    if (file == nullptr || (rs.samples_log2 != 0u)) {
        return;
    }
    static std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> seen;
    const std::uint32_t tail = static_cast<std::uint32_t>(rs.zmode_bits) | static_cast<std::uint32_t>(rs.cull) << 16u |
                               static_cast<std::uint32_t>(rs.pixfmt) << 20u | static_cast<std::uint32_t>(rs.primitive_topology) << 24u;
    if (seen.emplace(shader, rs.blend_bits, tail).second) {
        std::fprintf(file, "%u %u %u %u %u %u\n", shader, rs.blend_bits, static_cast<unsigned>(rs.zmode_bits),
                     static_cast<unsigned>(rs.cull), static_cast<unsigned>(rs.pixfmt), static_cast<unsigned>(rs.primitive_topology));
        std::fflush(file);
    }
}

}  // namespace galaxy::render
