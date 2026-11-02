// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/native_material.h"

#include "galaxy/render/draw_matrices.h"

#include <bit>

namespace galaxy::render::material {

// The record's encoding (J3DNativeTexGen).
TexGen tex_gen(std::uint8_t kind, std::uint8_t source) {
    return TexGen{static_cast<TexGenType>(kind & 3u), static_cast<std::uint8_t>(source & 15u),
                  static_cast<std::uint8_t>((source >> 4u) & 7u), static_cast<std::uint8_t>((kind >> 4u) & 7u)};
}

}  // namespace galaxy::render::material
