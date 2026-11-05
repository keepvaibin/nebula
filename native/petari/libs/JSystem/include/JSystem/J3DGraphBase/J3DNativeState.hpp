#pragma once

#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

// How a point light dims with distance.
enum J3DLightFalloff {
    J3D_FALLOFF_NONE = 0,
    J3D_FALLOFF_GENTLE,
    J3D_FALLOFF_MEDIUM,
    J3D_FALLOFF_STEEP,
};

// What a draw does to the target, whole: nothing carries over from the draw before.
typedef NebulaPassState J3DDrawState;

#ifdef __cplusplus
namespace J3DPass {
    void textureMatrix(u8 coord, u8 matrix);  // coordinate `coord` is texture coordinate 0 through texture matrix `matrix`
    void textureIdentity(u8 coord);           // coordinate `coord` is its own texture coordinate
};  // namespace J3DPass

extern "C" {
#endif
// The state of the draws that follow, whole (alphaWrite 2: the pass's).
void J3DNativeStateUse(u8 source, u8 destination, int depthTest, u8 compare, int depthWrite, u8 cull, int colorWrite, int alphaWrite);
#ifdef __cplusplus
}
#endif

// Drawing-state changes the game makes as it draws: kept here and carried by the draws that follow (the pass), or sent
// as a state record in their place in the stream (nebula_game_api.h state).
#ifdef __cplusplus
namespace J3DNativeState {
    // The state the draws that follow carry: the last use or material's. The host keeps none.
    const NebulaPassState* pass();
    void use(const J3DDrawState& rState);
    // A material to the host; with `takeState`, its blend, depth, culling, writes and stencil are the state from here.
    void sendMaterial(const NebulaNativeMaterial* pMaterial, bool takeState = true);
    // What belongs to the pass the draws are in: whether they write the target's alpha (a material asks for the pass's
    // with alpha_write 2) and the stencil of a material that names none.
    void setPassAlpha(bool write);
    void setPassStencil(u8 test, u8 op);
    // Lines and points of the draws that follow: pixels across; `spreadTexture`: texture coordinates 0-2 run 0 to 1
    // across the width of the line or the point.
    void lineWidth(f32 pixels, bool spreadTexture = false);
    void pointSize(f32 pixels, bool spreadTexture = false);
    inline void setBlend(J3DDrawState& rState, u8 source, u8 destination) {
        rState.blend_op = NEBULA_BLEND_ADD;
        rState.blend_src = source;
        rState.blend_dst = destination;
    }
    inline void setNoBlend(J3DDrawState& rState) {
        rState.blend_op = NEBULA_BLEND_REPLACE;
        rState.blend_src = 0;
        rState.blend_dst = 0;
    }
    inline void setDepth(J3DDrawState& rState, bool test, u8 compare, bool write) {
        rState.depth_test = test;
        rState.depth_func = compare & 7;
        rState.depth_write = write;
    }
    // Lights:
    void initLightColor(J3DNativeLight* pObj, NebulaColor8 color);
    void initLightPos(J3DNativeLight* pObj, f32 x, f32 y, f32 z);
    void initLightDir(J3DNativeLight* pObj, f32 nx, f32 ny, f32 nz);
    // A specular light: its direction as a light at infinity, with the half-angle vector (computed for a viewer down
    // -Z, or given).
    void initSpecularDir(J3DNativeLight* pObj, f32 nx, f32 ny, f32 nz);
    void initSpecularDirHA(J3DNativeLight* pObj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz);
    void initLightAttn(J3DNativeLight* pObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2);
    void initLightNoSpot(J3DNativeLight* pObj);
    void initLightDistAttn(J3DNativeLight* pObj, f32 refDistance, f32 refBrightness, J3DLightFalloff falloff);
    void sendLight(const J3DNativeLight* pLight, u8 index);  // light 0-7
    // Whether the frame keeps alpha (the last value kept here: the host changes only when it differs).
    // The drawing state the game starts from , sent before anything is drawn.
    void initDefaults();
    // The frame's picture is drawn: its top left `width` x `height` becomes the one shown, through the display's
    // vertical filter when `filtered`.
    void showFrame();
    // The game's frame is complete: the host renders what it drew.
    void endFrame();
};  // namespace J3DNativeState

extern "C" {
#endif
// The same for the C sources (MiiLib).
void J3DNativeStateInitLightColor(J3DNativeLight* pObj, NebulaColor8 color);
void J3DNativeStateInitLightPos(J3DNativeLight* pObj, f32 x, f32 y, f32 z);
void J3DNativeStateSendLight(const J3DNativeLight* pLight, u8 index);
#ifdef __cplusplus
}
#endif
