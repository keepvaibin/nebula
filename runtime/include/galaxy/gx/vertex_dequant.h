#pragma once

#include <bit>
#include <cassert>
#include <cstdint>

namespace galaxy::gx::detail {

// GX fixed-point vertex components have at most 16 integer bits and a 5-bit VAT fraction
// (0..31), so the conversion and the scaling by 2^-shift are exact in binary32, with no
// subnormal results. Building the power of two avoids a divide per component.
[[nodiscard]] constexpr float dequantize_vertex_integer(
    std::int32_t value, std::uint8_t shift) noexcept {
    assert(shift <= 31u);
    const float scale = std::bit_cast<float>((127u - shift) << 23u);
    return static_cast<float>(value) * scale;
}

} // namespace galaxy::gx::detail
