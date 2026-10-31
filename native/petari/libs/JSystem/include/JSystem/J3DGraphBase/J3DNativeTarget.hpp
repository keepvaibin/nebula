#pragma once

#include <revolution/types.h>
#include <nebula_game_api.h>

#ifdef __cplusplus
// A pass into a texture target the renderer owns (nebula_game_api.h target_begin/target_end): the draws between begin
// and end go to the target, not the frame, and the picture becomes the texture `pKey` names (the buffer the game
// would have copied it into: its images and texture loads read it).
namespace J3DNativeTarget {
    // Opens the pass: the target's top left `width` x `height` (in the viewport's units) cleared to `clearRgba`
    // (R << 24 | G << 16 | B << 8 | A) and depth `clearZ` (24 bits).
    void begin(const void* pKey, u16 width, u16 height, u32 clearRgba, u32 clearZ);
    // Closes it: the picture, at full precision, as texture `pKey`.
    void end(const void* pKey);
    // Clears channels of the open pass (`channels`: 1 colour, 2 alpha, 4 depth).
    void clear(u32 clearRgba, u32 clearZ, u32 channels);
};  // namespace J3DNativeTarget

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeTargetBegin(const void* pKey, u16 width, u16 height, u32 clearRgba, u32 clearZ);
void J3DNativeTargetEnd(const void* pKey);
void J3DNativeTargetClear(u32 clearRgba, u32 clearZ, u32 channels);
#ifdef __cplusplus
}
#endif
