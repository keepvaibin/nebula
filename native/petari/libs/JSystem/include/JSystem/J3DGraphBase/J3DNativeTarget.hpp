#pragma once

#include <nebula_game_api.h>

#ifdef __cplusplus
// A pass into a texture target the renderer owns (nebula_game_api.h target_begin/target_end): the draws between begin
// and end go to the target, not the frame, and the picture becomes the texture `pKey` names (the buffer the game
// would have copied it into: its images and texture loads read it).
namespace J3DNativeTarget {
    // Opens the pass: a target `across` x `down` of the frame (1, 1: its size), its whole picture cleared to `clearRgba`
    // (R << 24 | G << 16 | B << 8 | A) and depth to far.
    void begin(const void* pKey, f32 across, f32 down, u32 clearRgba);
    // The same at a size of its own, whatever the frame's.
    void beginPixels(const void* pKey, u32 width, u32 height, u32 clearRgba);
    // Closes it: the picture, at full precision, as texture `pKey`.
    void end(const void* pKey, NebulaTargetChannels channels = NEBULA_TARGET_RGBA);
    // Clears channels of the open pass (`channels`: 1 colour, 2 alpha, 4 depth).
    void clear(u32 clearRgba, u32 channels);
    // Clears channels (NebulaClearChannels) of the frame to `clearRgba` and depth to far.
    void clearFrame(u32 clearRgba, u32 channels);
    // A pass into `pKey` that samples the frame: texture `pFrameKey` is the frame carrying `channels`, texture `pDepthKey`
    // its depth, until the pass ends. A null key: not sampled.
    void beginOverFrame(const void* pKey, f32 across, f32 down, u32 clearRgba, const void* pFrameKey,
                        NebulaTargetChannels channels, const void* pDepthKey);
    // The whole frame drawn so far as texture `pKey`: an effect reading the picture while it is still being drawn.
    void framePass(const void* pKey);
    // The frame drawn so far becomes texture `pKey` itself; the frame goes on in another target, which the next draw
    // must fill whole.
    void take(const void* pKey);
};  // namespace J3DNativeTarget

extern "C" {
#endif
// The same for the C sources (MiiLib).
void J3DNativeTargetBeginPixels(const void* pKey, u32 width, u32 height, u32 clearRgba);
void J3DNativeTargetEnd(const void* pKey);
void J3DNativeTargetClear(u32 clearRgba, u32 channels);
#ifdef __cplusplus
}
#endif
