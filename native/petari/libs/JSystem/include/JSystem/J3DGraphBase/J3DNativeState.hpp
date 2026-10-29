#pragma once

#include <revolution.h>

// Drawing-state changes the game makes as it draws, each sent at once as a state record in its place in the stream
// (nebula_game_api.h state). Each takes the arguments of the GX function it is named after and sets the whole
// register that function sets.
#ifdef __cplusplus
namespace J3DNativeState {
    void setZMode(GXBool compare, GXCompare func, GXBool update);
    void setAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1);
    void setDstAlpha(GXBool enable, u8 alpha);
    void setTevColor(GXTevRegID id, GXColor color);
    void setTevKColor(GXTevKColorID id, GXColor color);
    void setTevColorS10(GXTevRegID id, GXColorS10 color);
    void setChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask,
                     GXDiffuseFn diffFn, GXAttnFn attnFn);
    void setTevDirect(GXTevStageID stage);
    // GXSetFog's writes (a projected type sends its second and third words without their register id, as it does).
    void setFog(GXFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, GXColor color);
    void setClipMode(GXClipMode mode);
    void setIndTexMtx(GXIndTexMtxID id, const f32 offset[2][3], s8 scaleExp);
    // Partial setters: each changes only its own fields of the register it shares.
    void setTexCoordGen2(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize, u32 postMtx);
    void setNumTexGens(u8 count);
    void setNumChans(u8 count);
    void setNumTevStages(u8 count);
    void setNumIndStages(u8 count);
    void setCoPlanar(GXBool enable);
    void setZCompLoc(GXBool beforeTex);
    void setCurrentMtx(u32 id);
    void setTevIndirect(GXTevStageID stage, GXIndTexStageID indStage, GXIndTexFormat format, GXIndTexBiasSel bias, GXIndTexMtxID matrix, GXIndTexWrap wrapS, GXIndTexWrap wrapT, GXBool addPrev, GXBool utcLod, GXIndTexAlphaSel alpha);
    void setTevIndWarp(GXTevStageID stage, GXIndTexStageID indStage, GXBool signedOffset, GXBool replaceMode, GXIndTexMtxID matrix);
    void setIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT);
    void setLineWidth(u8 width, GXTexOffset texOffsets);
    void setPointSize(u8 size, GXTexOffset texOffsets);
    // Lights: a GXLightObj holds a J3DNativeLight; the GXInitLight* functions' arithmetic, then the light sent.
    void initLightColor(GXLightObj* pObj, GXColor color);
    void initLightPos(GXLightObj* pObj, f32 x, f32 y, f32 z);
    void initLightDir(GXLightObj* pObj, f32 nx, f32 ny, f32 nz);
    // A specular light: its direction as a light at infinity, with the half-angle vector (computed for a viewer down
    // -Z, or given).
    void initSpecularDir(GXLightObj* pObj, f32 nx, f32 ny, f32 nz);
    void initSpecularDirHA(GXLightObj* pObj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz);
    void initLightAttn(GXLightObj* pObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2);
    void initLightSpot(GXLightObj* pObj, f32 cutoff, GXSpotFn spotFn);
    void initLightDistAttn(GXLightObj* pObj, f32 refDistance, f32 refBrightness, GXDistAttnFn distFn);
    void loadLightObjImm(const GXLightObj* pObj, GXLightID id);
    // The pixel format (the last one kept here: GXSetPixelFmt changes the hardware only when it differs).
    void setPixelFmt(GXPixelFmt format, GXZFmt16 zFormat);
    // Textures in memory changed.
    void invalidateTexAll();
    // EFB copies: their settings kept here (GX's defaults until set), each copy sent with them.
    void setDispCopySrc(u16 left, u16 top, u16 width, u16 height);
    void setTexCopySrc(u16 left, u16 top, u16 width, u16 height);
    void setDispCopyDst(u16 width, u16 height);
    void setTexCopyDst(u16 width, u16 height, GXTexFmt format, GXBool mipmap);
    void setDispCopyFrame2Field(GXCopyMode mode);
    void setCopyClamp(GXFBClamp clamp);
    void setDispCopyGamma(GXGamma gamma);
    u32 setDispCopyYScale(f32 scale);
    u16 getNumXfbLines(u16 efbHeight, f32 scale);
    f32 getYScaleFactor(u16 efbHeight, u16 xfbHeight);
    void setCopyClear(GXColor color, u32 depth);
    void setCopyFilter(GXBool antialias, const u8 samples[12][2], GXBool verticalFilter, const u8 coefficients[7]);
    void copyDisp(void* pDest, GXBool clear);
    void copyTex(void* pDest, GXBool clear);
    void setZTexture(GXZTexOp op, GXTexFmt format, u32 bias);
    void setFogRangeAdj(GXBool enable, u16 center, const GXFogAdjTable* pTable);
    void setTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color);
    void setIndTexOrder(GXIndTexStageID stage, GXTexCoordID coord, GXTexMapID map);
    void enableTexOffsets(GXTexCoordID coord, GXBool lines, GXBool points);
    // The cull mode (kept here: the game reads it back), GX's mapping.
    void setCullMode(GXCullMode mode);
    void getCullMode(GXCullMode* pMode);
    void setChanMatColor(GXChannelID chan, GXColor color);
    void setChanAmbColor(GXChannelID chan, GXColor color);
    void setBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op);
    void setTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d);
    void setTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d);
    void setTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out);
    void setTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out);
    void setTevKColorSel(GXTevStageID stage, GXTevKColorSel sel);
    void setTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel);
    void setTevSwapMode(GXTevStageID stage, GXTevSwapSel rasSwap, GXTevSwapSel texSwap);
    void setTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha);
    void setColorUpdate(GXBool enable);
    void setAlphaUpdate(GXBool enable);
    void setDither(GXBool enable);
    void setTevOp(GXTevStageID stage, GXTevMode mode);
};  // namespace J3DNativeState

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeStateSetTexCoordGen2(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize, u32 postMtx);
void J3DNativeStateSetNumTexGens(u8 count);
void J3DNativeStateSetNumChans(u8 count);
void J3DNativeStateSetNumTevStages(u8 count);
void J3DNativeStateSetNumIndStages(u8 count);
void J3DNativeStateSetCoPlanar(GXBool enable);
void J3DNativeStateSetZCompLoc(GXBool beforeTex);
void J3DNativeStateSetCurrentMtx(u32 id);
void J3DNativeStateSetTevIndirect(GXTevStageID stage, GXIndTexStageID indStage, GXIndTexFormat format, GXIndTexBiasSel bias, GXIndTexMtxID matrix, GXIndTexWrap wrapS, GXIndTexWrap wrapT, GXBool addPrev, GXBool utcLod, GXIndTexAlphaSel alpha);
void J3DNativeStateSetTevIndWarp(GXTevStageID stage, GXIndTexStageID indStage, GXBool signedOffset, GXBool replaceMode, GXIndTexMtxID matrix);
void J3DNativeStateSetIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT);
void J3DNativeStateSetLineWidth(u8 width, GXTexOffset texOffsets);
void J3DNativeStateSetPointSize(u8 size, GXTexOffset texOffsets);
void J3DNativeStateInitLightColor(GXLightObj* pObj, GXColor color);
void J3DNativeStateInitLightPos(GXLightObj* pObj, f32 x, f32 y, f32 z);
void J3DNativeStateLoadLightObjImm(const GXLightObj* pObj, GXLightID id);
void J3DNativeStateSetPixelFmt(GXPixelFmt format, GXZFmt16 zFormat);
void J3DNativeStateInvalidateTexAll(void);
void J3DNativeStateSetTexCopySrc(u16 left, u16 top, u16 width, u16 height);
void J3DNativeStateSetTexCopyDst(u16 width, u16 height, GXTexFmt format, GXBool mipmap);
void J3DNativeStateSetCopyClamp(GXFBClamp clamp);
void J3DNativeStateSetCopyClear(GXColor color, u32 depth);
void J3DNativeStateSetCopyFilter(GXBool antialias, const u8 samples[12][2], GXBool verticalFilter, const u8 coefficients[7]);
void J3DNativeStateCopyTex(void* pDest, GXBool clear);
void J3DNativeStateSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color);
void J3DNativeStateSetIndTexOrder(GXIndTexStageID stage, GXTexCoordID coord, GXTexMapID map);
void J3DNativeStateEnableTexOffsets(GXTexCoordID coord, GXBool lines, GXBool points);
void J3DNativeStateSetCullMode(GXCullMode mode);
void J3DNativeStateGetCullMode(GXCullMode* pMode);
void J3DNativeStateSetChanMatColor(GXChannelID chan, GXColor color);
void J3DNativeStateSetChanAmbColor(GXChannelID chan, GXColor color);
void J3DNativeStateSetZMode(GXBool compare, GXCompare func, GXBool update);
void J3DNativeStateSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1);
void J3DNativeStateSetDstAlpha(GXBool enable, u8 alpha);
void J3DNativeStateSetTevColor(GXTevRegID id, GXColor color);
void J3DNativeStateSetTevKColor(GXTevKColorID id, GXColor color);
void J3DNativeStateSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask, GXDiffuseFn diffFn, GXAttnFn attnFn);
void J3DNativeStateSetTevDirect(GXTevStageID stage);
void J3DNativeStateSetFog(GXFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, GXColor color);
void J3DNativeStateSetBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op);
void J3DNativeStateSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d);
void J3DNativeStateSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d);
void J3DNativeStateSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out);
void J3DNativeStateSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out);
void J3DNativeStateSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel);
void J3DNativeStateSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel);
void J3DNativeStateSetTevSwapMode(GXTevStageID stage, GXTevSwapSel rasSwap, GXTevSwapSel texSwap);
void J3DNativeStateSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha);
void J3DNativeStateSetColorUpdate(GXBool enable);
void J3DNativeStateSetAlphaUpdate(GXBool enable);
void J3DNativeStateSetDither(GXBool enable);
#ifdef __cplusplus
}
#endif
