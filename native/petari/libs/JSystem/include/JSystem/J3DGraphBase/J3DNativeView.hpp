#pragma once

#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

// The projection the game draws with: its values kept here (the game reads them back) and each change sent as a
// call (nebula_game_api.h view_projection): the type,
// 0 for perspective, then the six terms).
#ifdef __cplusplus
namespace J3DNativeView {
    // Frame units per unit of the game's screen.
    const f32 cUnitsAcross = NEBULA_FRAME_UNITS / 640.0f;
    const f32 cUnitsDown = NEBULA_FRAME_UNITS / 456.0f;

    void setPerspective(const f32 (*pMtx)[4]);
    void setOrthographic(const f32 (*pMtx)[4]);
    void setProjectionv(const f32* pValues);
    void getProjectionv(f32* pValues);
    // The viewport (field 0 of an interlaced pair moves it up half a line), and the depth
    // range's scale and offset that the viewport's z terms take.
    void setViewport(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ);
    void setViewportJitter(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ, u32 field);
    void getViewportv(f32* pValues);
    void setZScaleOffset(f32 scale, f32 offset);
    // The scissor box (the viewport's units).
    void setScissor(u32 left, u32 top, u32 width, u32 height);
    void getScissor(u32* pLeft, u32* pTop, u32* pWidth, u32* pHeight);
    // The picture's colour and depth where the drawing has reached, kept as `channel`'s; reads take the latest finished
    // capture (a frame or so old) and never wait. Frame-buffer coordinates.
    // What the picture holds at the game's points where the drawing has reached; the answers come back a frame or two later.
    void probeScreen(u32 channel, NebulaScreenProbeKind what, const NebulaScreenProbe* pProbes, u32 count);
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
void J3DNativeViewSetScissor(u32 left, u32 top, u32 width, u32 height);
void J3DNativeViewGetScissor(u32* pLeft, u32* pTop, u32* pWidth, u32* pHeight);
#ifdef __cplusplus
}
#endif
