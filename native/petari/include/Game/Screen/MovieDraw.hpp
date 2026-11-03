#pragma once


// A movie frame drawn from its decoded planes (Y at the frame's size, U and V at half), converted from YUV to RGB by
// the THP coefficients in a native material program.
namespace MovieDraw {
    // The planes are sampled linearly (else nearest).
    void setLinearFilter(bool linear);
    // The frame at `x`, `y` (`width` x `height`) on a `screenWidth` x `screenHeight` screen; the planes are
    // `textureWidth` x `textureHeight`.
    void drawFrame(u16 screenWidth, u16 screenHeight, u8* pY, u8* pU, u8* pV, s16 x, s16 y, s16 textureWidth,
                   s16 textureHeight, s16 width, s16 height);
};  // namespace MovieDraw
