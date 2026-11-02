#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

// Drawing-state changes the game makes as it draws, each sent at once as a state record in its place in the stream
// (nebula_game_api.h state). Each takes the arguments of the GX function it is named after and sets the whole
// register that function sets.
#ifdef __cplusplus
namespace J3DNativeState {
    void setZMode(GpuBool compare, GpuCompare func, GpuBool update);
    void setAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1);
    void setDstAlpha(GpuBool enable, u8 alpha);
    void setTevColor(GpuTevRegID id, NebulaColor8 color);
    void setTevKColor(GpuTevKColorID id, NebulaColor8 color);
    void setTevColorS10(GpuTevRegID id, NebulaColor16 color);
    void setChanCtrl(GpuChannelID chan, GpuBool enable, GpuColorSrc ambSrc, GpuColorSrc matSrc, u32 lightMask,
                     GpuDiffuseFn diffFn, GpuAttnFn attnFn);
    void setTevDirect(GpuTevStageID stage);
    // GpuSetFog's writes (a projected type sends its second and third words without their register id, as it does).
    void setFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
    void setClipMode(GpuClipMode mode);
    void setIndTexMtx(GpuIndTexMtxID id, const f32 offset[2][3], s8 scaleExp);
    // Partial setters: each changes only its own fields of the register it shares.
    void setTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx);
    void setNumTexGens(u8 count);
    void setNumChans(u8 count);
    void setNumTevStages(u8 count);
    void setNumIndStages(u8 count);
    void setCoPlanar(GpuBool enable);
    void setCurrentMtx(u32 id);
    void setTevIndirect(GpuTevStageID stage, GpuIndTexStageID indStage, GpuIndTexFormat format, GpuIndTexBiasSel bias, GpuIndTexMtxID matrix, GpuIndTexWrap wrapS, GpuIndTexWrap wrapT, GpuBool addPrev, GpuBool utcLod, GpuIndTexAlphaSel alpha);
    void setTevIndWarp(GpuTevStageID stage, GpuIndTexStageID indStage, GpuBool signedOffset, GpuBool replaceMode, GpuIndTexMtxID matrix);
    void setIndTexCoordScale(GpuIndTexStageID indStage, GpuIndTexScale scaleS, GpuIndTexScale scaleT);
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
    void setZTexture(GpuZTexOp op, GpuTexFmt format, u32 bias);
    void setFogRangeAdj(GpuBool enable, u16 center, const GpuFogAdjTable* pTable);
    void setTevOrder(GpuTevStageID stage, GpuTexCoordID coord, GpuTexMapID map, GpuChannelID color);
    void setIndTexOrder(GpuIndTexStageID stage, GpuTexCoordID coord, GpuTexMapID map);
    void enableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points);
    // The cull mode (kept here: the game reads it back), GX's mapping.
    void setCullMode(GpuCullMode mode);
    void getCullMode(GpuCullMode* pMode);
    void setChanMatColor(GpuChannelID chan, NebulaColor8 color);
    void setChanAmbColor(GpuChannelID chan, NebulaColor8 color);
    void setBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op);
    void setTevColorIn(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d);
    void setTevAlphaIn(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c, GpuTevAlphaArg d);
    void setTevColorOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out);
    void setTevAlphaOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out);
    void setTevKColorSel(GpuTevStageID stage, GpuTevKColorSel sel);
    void setTevKAlphaSel(GpuTevStageID stage, GpuTevKAlphaSel sel);
    void setTevSwapMode(GpuTevStageID stage, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap);
    void setTevSwapModeTable(GpuTevSwapSel table, GpuTevColorChan red, GpuTevColorChan green, GpuTevColorChan blue, GpuTevColorChan alpha);
    void setColorUpdate(GpuBool enable);
    void setAlphaUpdate(GpuBool enable);
    void setDither(GpuBool enable);
    void setTevOp(GpuTevStageID stage, GpuTevMode mode);
};  // namespace J3DNativeState

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeStateSetTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx);
void J3DNativeStateSetNumTexGens(u8 count);
void J3DNativeStateSetNumChans(u8 count);
void J3DNativeStateSetNumTevStages(u8 count);
void J3DNativeStateSetNumIndStages(u8 count);
void J3DNativeStateSetCoPlanar(GpuBool enable);
void J3DNativeStateSetCurrentMtx(u32 id);
void J3DNativeStateSetTevIndirect(GpuTevStageID stage, GpuIndTexStageID indStage, GpuIndTexFormat format, GpuIndTexBiasSel bias, GpuIndTexMtxID matrix, GpuIndTexWrap wrapS, GpuIndTexWrap wrapT, GpuBool addPrev, GpuBool utcLod, GpuIndTexAlphaSel alpha);
void J3DNativeStateSetTevIndWarp(GpuTevStageID stage, GpuIndTexStageID indStage, GpuBool signedOffset, GpuBool replaceMode, GpuIndTexMtxID matrix);
void J3DNativeStateSetIndTexCoordScale(GpuIndTexStageID indStage, GpuIndTexScale scaleS, GpuIndTexScale scaleT);
void J3DNativeStateSetLineWidth(u8 width, GpuTexOffset texOffsets);
void J3DNativeStateSetPointSize(u8 size, GpuTexOffset texOffsets);
void J3DNativeStateInitLightColor(GpuLightObj* pObj, NebulaColor8 color);
void J3DNativeStateInitLightPos(GpuLightObj* pObj, f32 x, f32 y, f32 z);
void J3DNativeStateLoadLightObjImm(const GpuLightObj* pObj, GpuLightID id);
void J3DNativeStateSetTevOrder(GpuTevStageID stage, GpuTexCoordID coord, GpuTexMapID map, GpuChannelID color);
void J3DNativeStateSetIndTexOrder(GpuIndTexStageID stage, GpuTexCoordID coord, GpuTexMapID map);
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
void J3DNativeStateSetChanCtrl(GpuChannelID chan, GpuBool enable, GpuColorSrc ambSrc, GpuColorSrc matSrc, u32 lightMask, GpuDiffuseFn diffFn, GpuAttnFn attnFn);
void J3DNativeStateSetTevDirect(GpuTevStageID stage);
void J3DNativeStateSetFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
void J3DNativeStateSetBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op);
void J3DNativeStateSetTevColorIn(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d);
void J3DNativeStateSetTevAlphaIn(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c, GpuTevAlphaArg d);
void J3DNativeStateSetTevColorOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out);
void J3DNativeStateSetTevAlphaOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out);
void J3DNativeStateSetTevKColorSel(GpuTevStageID stage, GpuTevKColorSel sel);
void J3DNativeStateSetTevKAlphaSel(GpuTevStageID stage, GpuTevKAlphaSel sel);
void J3DNativeStateSetTevSwapMode(GpuTevStageID stage, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap);
void J3DNativeStateSetTevSwapModeTable(GpuTevSwapSel table, GpuTevColorChan red, GpuTevColorChan green, GpuTevColorChan blue, GpuTevColorChan alpha);
void J3DNativeStateSetColorUpdate(GpuBool enable);
void J3DNativeStateSetAlphaUpdate(GpuBool enable);
void J3DNativeStateSetDither(GpuBool enable);
#ifdef __cplusplus
}
#endif
