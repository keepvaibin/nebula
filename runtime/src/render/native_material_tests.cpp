// Native materials: the matrix memory names exactly the blocks a write changed.

#include "galaxy/render/drawing_state.h"
#include "galaxy/render/native_material.h"
#include "galaxy/render/pso_key.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

using namespace galaxy::render;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAILED: %s\n", what);
    }
}

struct Random {
    std::mt19937 rng{20261030u};
    unsigned operator()(unsigned n) { return static_cast<unsigned>(rng() % n); }
    float real(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(rng() % 100000u) / 100000.0f; }
};

void changed_kinds_name_the_changed_blocks() {
    Random r;
    bool ok = true;
    DrawMatrices matrices;
    matrices.clear_changed();
    for (unsigned trial = 0; trial < 5000u && ok; ++trial) {
        const unsigned kind = r(4u);
        const unsigned place = r(10u);
        float values[12];
        // Half the writes repeat what is there.
        const bool same = r(2u) == 0u;
        const std::uint32_t first = kind == 0u ? draw_rows::kPosition + 3u * place
                                    : kind == 1u ? draw_rows::kNormal + 3u * place
                                    : kind == 2u ? draw_rows::kTexture + 3u * place
                                                 : draw_rows::kPost + 3u * place;
        const unsigned width = kind == 1u ? 3u : 4u;
        for (unsigned row = 0; row < 3u; ++row) {
            for (unsigned c = 0; c < width; ++c) {
                values[row * width + c] =
                    same ? std::bit_cast<float>(matrices.row(first + row)[c]) : r.real(-8.0f, 8.0f);
            }
        }
        bool differs = false;
        for (unsigned row = 0; row < 3u; ++row) {
            for (unsigned c = 0; c < 4u; ++c) {
                const std::uint32_t want = c < width ? std::bit_cast<std::uint32_t>(values[row * width + c]) : 0u;
                differs = differs || matrices.row(first + row)[c] != want;
            }
        }
        const std::uint32_t bit = kind == 0u ? DrawMatrices::kPositions
                                  : kind == 1u ? DrawMatrices::kNormals
                                  : kind == 2u ? DrawMatrices::kTextures
                                               : DrawMatrices::kPosts;
        if (kind == 0u) {
            matrices.set_position(place, values, 0u);
        } else if (kind == 1u) {
            matrices.set_normal(place, values, 0u);
        } else if (kind == 2u) {
            matrices.set_texture(place, values, 3u);
        } else {
            matrices.set_post(place, values);
        }
        ok = matrices.changed() == (differs ? bit : 0u);
        matrices.clear_changed();
    }
    check(ok, "a write marks its kind changed when its values differ, none when they are equal");
    if (ok) {
        std::printf("ok: changed kinds name the changed blocks\n");
    }
}

}  // namespace

int main() {
    changed_kinds_name_the_changed_blocks();
    if (g_failures != 0) {
        std::printf("%d native material checks FAILED\n", g_failures);
        return 1;
    }
    return 0;
}
