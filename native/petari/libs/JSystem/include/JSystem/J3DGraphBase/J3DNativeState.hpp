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

// What the draws that follow do to the target, in the host's values (nebula_game_api.h).
#ifdef __cplusplus
namespace J3DPass {
    void depth(bool test, u8 compare, bool write);  // NEBULA_COMPARE_*
    void stencil(u8 test, u8 op);                   // NEBULA_STENCIL_*
    void blend(u8 source, u8 destination);          // source * source + target * destination, NEBULA_FACTOR_*
    void blendState(u8 op, u8 source, u8 destination);  // NEBULA_BLEND_*, NEBULA_FACTOR_*
    void noBlend();
    void subtractBlend();
    void cull(u8 faces);  // NEBULA_CULL_*
    void writeColor(bool write);
    void writeAlpha(bool write);
    void positionMatrix(u8 index);          // the place vertices without one of their own use
    // Pixels across; `spreadTexture`: texture coordinates 0-2 run 0 to 1 across the width of the line or the point.
    void lineWidth(f32 pixels, bool spreadTexture = false);
    void pointSize(f32 pixels, bool spreadTexture = false);
    void textureMatrix(u8 coord, u8 matrix);  // coordinate `coord` is texture coordinate 0 through texture matrix `matrix`
    void textureIdentity(u8 coord);           // coordinate `coord` is its own texture coordinate
};  // namespace J3DPass

extern "C" {
#endif
void J3DPassDepth(int test, u8 compare, int write);
void J3DPassBlend(u8 source, u8 destination);
void J3DPassCull(u8 faces);
void J3DPassWriteColor(int write);
void J3DPassWriteAlpha(int write);
void J3DPassPositionMatrix(u8 index);
#ifdef __cplusplus
}
#endif

// Drawing-state changes the game makes as it draws: kept here and carried by the draws that follow (the pass), or sent
// as a state record in their place in the stream (nebula_game_api.h state).
#ifdef __cplusplus
namespace J3DNativeState {
    // What a draw does to the target: the blend, depth, culling and writes last set or last brought by a material. Each
    // draw carries them; the host keeps none.
    const NebulaPassState* pass();
    // A material to the host; its blend, depth, culling and writes become the current ones.
    void sendMaterial(const NebulaNativeMaterial* pMaterial);
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
