#pragma once
#include <cstdint>
#include <span>

namespace galaxy::thp {
// Bounded native THP subsystem boundary. Outputs use GX I8 8x4 tiling.
// Adapted from Aurora MIT THPDec.cpp; see implementation for provenance.
std::int32_t decode_video(std::span<const std::uint8_t> input,
    std::uint16_t width, std::uint16_t height,
    std::span<std::uint8_t> y, std::span<std::uint8_t> u,
    std::span<std::uint8_t> v);
}
