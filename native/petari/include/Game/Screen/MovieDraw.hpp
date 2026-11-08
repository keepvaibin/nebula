#pragma once


// A movie frame drawn from its decoded planes (Y at the frame's size, U and V at half), converted from YUV to RGB by
// the THP coefficients in a native material program.
namespace MovieDraw {
    // The planes are sampled linearly (else nearest).
    void setLinearFilter(bool linear);
    // The frame's planes, decoded since the last upload, as textures.
    void uploadPlanes(u8* pY, u8* pU, u8* pV, u16 textureWidth, u16 textureHeight);
    // The frame at `x`, `y` (`width` x `height`) on a `screenWidth` x `screenHeight` screen; the planes are
    // `textureWidth` x `textureHeight`.
    void drawFrame(f32 screenWidth, f32 screenHeight, u8* pY, u8* pU, u8* pV, u16 textureWidth, u16 textureHeight, f32 x,
                   f32 y, f32 width, f32 height);
};  // namespace MovieDraw
