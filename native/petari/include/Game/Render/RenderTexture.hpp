#pragma once

#include <NebulaGpu.h>
#include <nebula_game_api.h>

// The renderer's numbers for the game's textures, by the address of their image: a texture record (registered when first
// used), an image in a plain format (RenderTexture::linear) or a key the game draws into (a target).
namespace RenderTexture {
    u32 idOf(const void* pImage);
    // An image in a plain format: handed over, or handed over again when it was before.
    u32 linear(const void* pImage, u8 format, u32 width, u32 height, u32 levels);
    u32 linearSize(u8 format, u32 width, u32 height, u32 levels);
    // The game rewrote this memory: images there are handed over again when next used.
    void rewritten(const void* pAddress, u32 size);
    // This memory is going.
    void forget(uintptr_t start, uintptr_t end);
}  // namespace RenderTexture
