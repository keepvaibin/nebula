// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cstdint>

namespace galaxy::render {

// How a texture's texels sit in memory, on the GPU as in its record (nebula_cooked_texture.h).
// R8: the value in every channel. Rg8: r = g = b = the first byte, a = the second. Bc1: 4x4 blocks of 8 bytes. Astc4x4:
// 4x4 blocks of 16 bytes (a BLOCK4 record's blocks on a GPU without BC1: block4_astc.h).
enum class TexelFormat : std::uint8_t { Rgba8, R8, Rg8, Bc1, Astc4x4 };

// A level's rows as a copy takes them: how many, and the bytes of each.
[[nodiscard]] inline std::uint32_t texel_rows(TexelFormat format, std::uint32_t height) {
    return format == TexelFormat::Bc1 || format == TexelFormat::Astc4x4 ? (height + 3u) / 4u : height;
}
[[nodiscard]] inline std::uint32_t texel_row_bytes(TexelFormat format, std::uint32_t width) {
    switch (format) {
    case TexelFormat::R8: return width;
    case TexelFormat::Rg8: return width * 2u;
    case TexelFormat::Bc1: return (width + 3u) / 4u * 8u;
    case TexelFormat::Astc4x4: return (width + 3u) / 4u * 16u;
    default: return width * 4u;
    }
}

}  // namespace galaxy::render
