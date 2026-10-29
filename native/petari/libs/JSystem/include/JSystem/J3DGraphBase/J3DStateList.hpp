#pragma once

#include <nebula_game_api.h>
#include <revolution.h>

// A light as the game sets it up (GXLightObj's values: the GXInitLight* functions' results).
struct J3DNativeLight {
    u8 mColor[4];
    f32 mAngle[3];     // a0-a2
    f32 mDistance[3];  // k0-k2
    f32 mPosition[3];
    f32 mDirection[3];
};

// Drawing state the game sets outside materials, as values (nebula_game_api.h NebulaStateOp), sent as one record in
// draw order. Each setter takes the arguments of the GX function its change is named after.
class J3DStateList {
public:
    J3DStateList() : mOps(nullptr), mCount(0), mCapacity(0) {}
    // A list writing into `pStorage` (`capacity` changes), for one being built.
    J3DStateList(NebulaStateOp* pStorage, u16 capacity) : mOps(pStorage), mCount(0), mCapacity(capacity) {}

    // Storage for `capacity` changes, from the current heap.
    void reserve(u16 capacity);
    // `rOther`'s changes (storage sized to them from the current heap the first time).
    void copy(const J3DStateList& rOther);
    void clear() { mCount = 0; }
    // The first `count` changes of its storage, written there directly.
    void adopt(u16 count) { mCount = count; }
    bool isEmpty() const { return mCount == 0; }
    // The changes, as a record after every command written before it.
    void send() const;

    void setBlendModeEtc(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op, GXBool colorUpdate,
                         GXBool alphaUpdate, GXBool dither);
    void setBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op);
    void setZMode(GXBool compare, GXCompare func, GXBool update);
    void setAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1);
    void setDstAlpha(GXBool enable, u8 alpha);
    void setCullMode(GXCullMode mode);
    void setGenMode(u8 texGens, u8 chans, u8 tevs);
    void setGenMode2(u8 texGens, u8 chans, u8 tevs, u8 inds, GXCullMode cull);
    void setChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask,
                     GXDiffuseFn diffFn, GXAttnFn attnFn);
    void setTevColorCalc(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d,
                         GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out);
    void setTevAlphaCalcAndSwap(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c,
                                GXTevAlphaArg d, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp,
                                GXTevRegID out, GXTevSwapSel rasSwap, GXTevSwapSel texSwap);
    void setTevDirect(GXTevStageID stage);
    void setTevOrder(GXTevStageID evenStage, GXTexCoordID coord0, GXTexMapID map0, GXChannelID color0,
                     GXTexCoordID coord1, GXTexMapID map1, GXChannelID color1);
    void setTevColor(GXTevRegID reg, GXColor color);
    void setChanMatColor(GXChannelID chan, GXColor color);
    void setChanAmbColor(GXChannelID chan, GXColor color);
    void setTexCoordGen(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, GXBool normalize, u32 postMtx);
    void setTexLookupMode(GXTexMapID map, GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXTexFilter minFilter,
                          GXTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias, GXBool biasClamp,
                          GXBool edgeLod, GXAnisotropy maxAniso);
    void setTexImgAttr(GXTexMapID map, u16 width, u16 height, GXTexFmt format);
    void setTexImgPtr(GXTexMapID map, const void* pImage);
    // A colour-indexed texture's palette: its format and place (a GX TLUT name).
    void setTexTlut(GXTexMapID map, GXTlutFmt format, u32 tlutName);
    void loadTlut(u32 tlutName, const void* pLut);
    void setFog(GXFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, GXColor color);
    // Changes of some fields of a register, its others as they are (GX's partial setters).
    void setTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d);
    void setTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d);
    void setTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out);
    void setTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out);
    void setTevKColorSel(GXTevStageID stage, GXTevKColorSel sel);
    void setTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel);
    void setTevSwapMode(GXTevStageID stage, GXTevSwapSel rasSwap, GXTevSwapSel texSwap);
    void setTevStageOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color);
    void setIndTexOrder(GXIndTexStageID stage, GXTexCoordID coord, GXTexMapID map);
    void enableTexOffsets(GXTexCoordID coord, GXBool lines, GXBool points);
    void setColorUpdate(GXBool enable);
    void setAlphaUpdate(GXBool enable);
    void setDither(GXBool enable);
    void setTevKColor(GXTevKColorID id, GXColor color);
    void setTevColorS10(GXTevRegID id, GXColorS10 color);
    void setClipMode(GXClipMode mode);
    void setTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue,
                             GXTevColorChan alpha);
    // GXSetTevOp's preset as its four changes (inputs and operations of both halves).
    void setTevOp(GXTevStageID stage, GXTevMode mode);
    // GXSetTexCoordGen2's change: setTexCoordGen's and the coordinate's matrix index.
    void setTexCoordGen2(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize, u32 postMtx);
    void setNumTexGens(u8 count);
    void setNumChans(u8 count);
    void setNumTevStages(u8 count);
    void setNumIndStages(u8 count);
    void setCoPlanar(GXBool enable);
    void setZCompLoc(GXBool beforeTex);
    void setCurrentMtx(u32 id);
    void setTevIndirect(GXTevStageID stage, GXIndTexStageID indStage, GXIndTexFormat format, GXIndTexBiasSel bias,
                        GXIndTexMtxID matrix, GXIndTexWrap wrapS, GXIndTexWrap wrapT, GXBool addPrev, GXBool utcLod,
                        GXIndTexAlphaSel alpha);
    // GXSetTevIndWarp's setTevIndirect.
    void setTevIndWarp(GXTevStageID stage, GXIndTexStageID indStage, GXBool signedOffset, GXBool replaceMode,
                       GXIndTexMtxID matrix);
    void setIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT);
    void setLineWidth(u8 width, GXTexOffset texOffsets);
    void setPointSize(u8 size, GXTexOffset texOffsets);
    // A light (GXLoadLightObjImm's: its attenuation and colour, then its position and direction).
    void setLight(GXLightID id, const J3DNativeLight& rLight);
    // GXSetPixelFmt's change: `changed` when the format or depth format differs from the last one set.
    void setPixelFmt(GXPixelFmt format, GXZFmt16 zFormat, bool changed);
    void invalidateTexAll();
    void setZTexture(GXZTexOp op, GXTexFmt format, u32 bias);
    void setCopyClear(GXColor color, u32 depth);
    // GXSetCopyFilter's two changes (sample locations, vertical filter).
    void setCopyFilter(GXBool antialias, const u8 samples[12][2], GXBool verticalFilter, const u8 coefficients[7]);
    void setCopyYScale(u32 scale);
    void copy(const NebulaStateOp& rCopy);
    // GXSetFogRangeAdj's changes: an enabled one's table pairs, then the centre and enable.
    void setFogRangeAdj(GXBool enable, u16 center, const GXFogAdjTable* pTable);

private:
    NebulaStateOp& add(u8 kind);

    NebulaStateOp* mOps;
    u16 mCount;
    u16 mCapacity;
};
