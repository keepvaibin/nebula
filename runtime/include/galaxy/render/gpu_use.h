#pragma once

// What a pass does with a texture, said once for every backend: Direct3D 12 turns a change of use into a legacy state
// transition or an enhanced barrier (sync, access, layout), Vulkan into a synchronization2 image barrier, Direct3D 11
// into nothing (its runtime tracks hazards). E13-BARRIERS-DESIGN.md.

#include <cstdint>

namespace galaxy::render::gpu {

enum class Use : std::uint8_t {
    ColorWrite,     // a render target
    DepthWrite,     // depth test and write
    DepthRead,      // depth test without writes, and sampled by pixel shaders at the same time
    PixelRead,      // sampled by pixel shaders
    ComputeRead,    // read by compute shaders
    ShaderRead,     // sampled by any stage (compute and pixel)
    ComputeWrite,   // unordered access from compute (textures and buffers)
    CopySource,
    CopyDest,
    ResolveSource,
    ResolveDest,
    Present,
};

// A change of use whose old contents the next pass overwrites whole (a released picture, a bloom level, an AO
// target): no decompression of what was there.
enum class Contents : std::uint8_t { Keep, Discard };

}  // namespace galaxy::render::gpu
