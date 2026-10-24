#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::thp {

// The decoder's fused multiply-adds: Automatic uses FMA3 when the CPU has it.
// Both kernels give identical planes; Fma3 on a CPU without it is an error.
// Fma3: the hardware fused multiply-add (FMA3 on x86, the base fmadd on AArch64).
enum class VideoKernel : std::uint8_t { Automatic, ExactSse2, Fma3 };

// Bounded native THP subsystem boundary. Outputs use GX I8 8x4 tiling.
// Adapted from Aurora MIT THPDec.cpp; see implementation for provenance.
std::int32_t decode_video(std::span<const std::uint8_t> input,
    std::uint16_t width, std::uint16_t height,
    std::span<std::uint8_t> y, std::span<std::uint8_t> u,
    std::span<std::uint8_t> v, VideoKernel kernel = VideoKernel::Automatic);
}
