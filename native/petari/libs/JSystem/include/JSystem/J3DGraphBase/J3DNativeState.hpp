#pragma once

#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

// Drawing-state changes the game makes as it draws: kept here and carried by the draws that follow (the pass), or sent
// as a state record in their place in the stream (nebula_game_api.h state).
#ifdef __cplusplus
namespace J3DNativeState {
    // What a draw does to the target: the blend, depth, culling and writes last set or last brought by a material. Each
    // draw carries them; the host keeps none.
    const NebulaPassState* pass();
    // The pass changes among `count` state changes (J3DStateList::send).
    void passChanges(const NebulaStateOp* pOps, u32 count);
    // A material to the host; its blend, depth, culling and writes become the current ones.
    void sendMaterial(const NebulaNativeMaterial* pMaterial);
    void setZMode(GpuBool compare, GpuCompare func, GpuBool update);
    void setAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1);
    void setDstAlpha(GpuBool enable, u8 alpha);
    void setTevColor(GpuTevRegID id, NebulaColor8 color);
    void setTevKColor(GpuTevKColorID id, NebulaColor8 color);
    void setTevColorS10(GpuTevRegID id, NebulaColor16 color);
    // GpuSetFog's writes (a projected type sends its second and third words without their register id, as it does).
    void setFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
    void setIndTexMtx(GpuIndTexMtxID id, const f32 offset[2][3], s8 scaleExp);
    // Partial setters: each changes only its own fields of the register it shares.
    void setTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx);
    void setCurrentMtx(u32 id);
    void setLineWidth(u8 width, GpuTexOffset texOffsets);
    void setPointSize(u8 size, GpuTexOffset texOffsets);
    // Lights: a GpuLightObj holds a J3DNativeLight; the GpuInitLight* functions' arithmetic, then the light sent.
    void initLightColor(GpuLightObj* pObj, NebulaColor8 color);
    void initLightPos(GpuLightObj* pObj, f32 x, f32 y, f32 z);
    void initLightDir(GpuLightObj* pObj, f32 nx, f32 ny, f32 nz);
    // A specular light: its direction as a light at infinity, with the half-angle vector (computed for a viewer down
    // -Z, or given).
    void initSpecularDir(GpuLightObj* pObj, f32 nx, f32 ny, f32 nz);
    void initSpecularDirHA(GpuLightObj* pObj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz);
    void initLightAttn(GpuLightObj* pObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2);
    void initLightSpot(GpuLightObj* pObj, f32 cutoff, GpuSpotFn spotFn);
    void initLightDistAttn(GpuLightObj* pObj, f32 refDistance, f32 refBrightness, GpuDistAttnFn distFn);
    void loadLightObjImm(const GpuLightObj* pObj, GpuLightID id);
    // Whether the frame keeps alpha (the last value kept here: the host changes only when it differs).
    void setFrameAlpha(bool keepAlpha);
    // The drawing state the game starts from (what GpuInit set on the console), sent before anything is drawn.
    void initDefaults();
    // The frame's picture is drawn: its top left `width` x `height` becomes the one shown, through the display's
    // vertical filter when `filtered`.
    void showFrame(u16 width, u16 height, bool filtered);
    // The game's frame is complete: the host renders what it drew.
    void endFrame();
    void enableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points);
    // The cull mode (kept here: the game reads it back), the console's mapping.
    void setCullMode(GpuCullMode mode);
    void getCullMode(GpuCullMode* pMode);
    void setChanMatColor(GpuChannelID chan, NebulaColor8 color);
    void setChanAmbColor(GpuChannelID chan, NebulaColor8 color);
    void setBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op);
    void setColorUpdate(GpuBool enable);
    void setAlphaUpdate(GpuBool enable);
};  // namespace J3DNativeState

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeStateSetTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx);
void J3DNativeStateSetCurrentMtx(u32 id);
void J3DNativeStateSetLineWidth(u8 width, GpuTexOffset texOffsets);
void J3DNativeStateSetPointSize(u8 size, GpuTexOffset texOffsets);
void J3DNativeStateInitLightColor(GpuLightObj* pObj, NebulaColor8 color);
void J3DNativeStateInitLightPos(GpuLightObj* pObj, f32 x, f32 y, f32 z);
void J3DNativeStateLoadLightObjImm(const GpuLightObj* pObj, GpuLightID id);
void J3DNativeStateEnableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points);
void J3DNativeStateSetCullMode(GpuCullMode mode);
void J3DNativeStateGetCullMode(GpuCullMode* pMode);
void J3DNativeStateSetChanMatColor(GpuChannelID chan, NebulaColor8 color);
void J3DNativeStateSetChanAmbColor(GpuChannelID chan, NebulaColor8 color);
void J3DNativeStateSetZMode(GpuBool compare, GpuCompare func, GpuBool update);
void J3DNativeStateSetAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1);
void J3DNativeStateSetDstAlpha(GpuBool enable, u8 alpha);
void J3DNativeStateSetTevColor(GpuTevRegID id, NebulaColor8 color);
void J3DNativeStateSetTevKColor(GpuTevKColorID id, NebulaColor8 color);
void J3DNativeStateSetFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
void J3DNativeStateSetBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op);
void J3DNativeStateSetColorUpdate(GpuBool enable);
void J3DNativeStateSetAlphaUpdate(GpuBool enable);
#ifdef __cplusplus
}
#endif
