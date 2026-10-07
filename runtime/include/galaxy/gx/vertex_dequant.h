#pragma once

#include <bit>
#include <cassert>
#include <cstdint>

namespace galaxy::gx::detail {

// GX fixed vertex inputs have at most 16 significant integer bits and a
// five-bit VAT fraction (0..31). Both integer conversion and scaling are exact
// binary32 operations throughout that range; no subnormal result is possible.
// Constructing 2^-shift avoids a variable floating divide for every component,
// without reciprocal approximation, fast-math, or changes to guest arithmetic.
[[nodiscard]] constexpr float dequantize_vertex_integer(
    std::int32_t value, std::uint8_t shift) noexcept {
    assert(shift <= 31u);
    const float scale = std::bit_cast<float>((127u - shift) << 23u);
    return static_cast<float>(value) * scale;
}

} // namespace galaxy::gx::detail
