// THP movie frame decoder.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::thp {

// The decoder's fused multiply-add: ExactSse2 computes it exactly in SSE2, Fma3 uses the instruction (FMA3 on x86,
// fmadd on AArch64; an error on a CPU without it), Automatic takes Fma3 when the CPU has it. Both give identical planes.
enum class VideoKernel : std::uint8_t { Automatic, ExactSse2, Fma3 };

// Decodes one frame into Y, U and V planes in GX I8 8x4 tiles; 0 on success, else a decoder error code.
// Adapted from Aurora's THPDec.cpp (MIT; native_thp_video.cpp carries the notice).
std::int32_t decode_video(std::span<const std::uint8_t> input,
    std::uint16_t width, std::uint16_t height,
    std::span<std::uint8_t> y, std::span<std::uint8_t> u,
    std::span<std::uint8_t> v, VideoKernel kernel = VideoKernel::Automatic);
}
