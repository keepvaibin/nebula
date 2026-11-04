#pragma once

#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

// The projection the game draws with: its values kept here (the game reads them back) and each change sent as a
// call (nebula_game_api.h view_projection): the type,
// 0 for perspective, then the six terms).
#ifdef __cplusplus
namespace J3DNativeView {
    // Rectangles are fractions of the pass's target (the frame, or a texture target): 0, 0, 1, 1 is all of it.
    void setPerspective(const f32 (*pMtx)[4]);
    // What setPerspective and setViewport would hand the host for these values, the state left as it is: the six
    // projection terms, then the viewport's six values.
    void hostView(const f32 (*pMtx)[4], f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ, f32* pOut);
    void setOrthographic(const f32 (*pMtx)[4]);
    void setProjectionv(const f32* pValues);
    void getProjectionv(f32* pValues);
    // The viewport, and the depth range's scale and offset that the viewport's z terms take.
    void setViewport(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ);
    void getViewportv(f32* pValues);
    void setZScaleOffset(f32 scale, f32 offset);
    // The scissor box.
    void setScissor(f32 left, f32 top, f32 width, f32 height);
    void getScissor(f32* pLeft, f32* pTop, f32* pWidth, f32* pHeight);
    // The picture's colour and depth where the drawing has reached, kept as `channel`'s; reads take the latest finished
    // capture (a frame or so old) and never wait. Frame-buffer coordinates.
    // What the picture holds at the game's points where the drawing has reached; the answers come back a frame or two later.
    void probeScreen(u32 channel, const NebulaScreenProbe* pProbes, u32 count);
    u32 screenProbeResults(u32 channel, NebulaScreenProbe* pProbes, u32 capacity);
};  // namespace J3DNativeView

extern "C" {
#endif
// The same for the C sources (MiiLib).
void J3DNativeViewSetPerspective(const f32 (*pMtx)[4]);
void J3DNativeViewSetOrthographic(const f32 (*pMtx)[4]);
void J3DNativeViewSetViewport(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ);
void J3DNativeViewGetViewportv(f32* pValues);
void J3DNativeViewSetZScaleOffset(f32 scale, f32 offset);
void J3DNativeViewSetScissor(f32 left, f32 top, f32 width, f32 height);
void J3DNativeViewGetScissor(f32* pLeft, f32* pTop, f32* pWidth, f32* pHeight);
#ifdef __cplusplus
}
#endif
