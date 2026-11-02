#pragma once

#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <revolution/os.h>

// A light as the game sets it up (GpuLightObj's values: the GpuInitLight* functions' results).
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
    const NebulaStateOp* ops() const { return mOps; }
    u16 count() const { return mCount; }
    // The changes, as a record after every command written before it.
    void send() const;

    void setBlendModeEtc(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op, GpuBool colorUpdate,
                         GpuBool alphaUpdate, GpuBool dither);
    void setBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op);
    void setZMode(GpuBool compare, GpuCompare func, GpuBool update);
    void setAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1);
    void setDstAlpha(GpuBool enable, u8 alpha);
    void setCullMode(GpuCullMode mode);
    void setGenMode(u8 texGens, u8 chans, u8 tevs);
    void setGenMode2(u8 texGens, u8 chans, u8 tevs, u8 inds, GpuCullMode cull);
    void setChanCtrl(GpuChannelID chan, GpuBool enable, GpuColorSrc ambSrc, GpuColorSrc matSrc, u32 lightMask,
                     GpuDiffuseFn diffFn, GpuAttnFn attnFn);
    void setTevColorCalc(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d,
                         GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out);
    void setTevAlphaCalcAndSwap(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c,
                                GpuTevAlphaArg d, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp,
                                GpuTevRegID out, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap);
    void setTevDirect(GpuTevStageID stage);
    void setTevOrder(GpuTevStageID evenStage, GpuTexCoordID coord0, GpuTexMapID map0, GpuChannelID color0,
                     GpuTexCoordID coord1, GpuTexMapID map1, GpuChannelID color1);
    void setTevColor(GpuTevRegID reg, NebulaColor8 color);
    void setChanMatColor(GpuChannelID chan, NebulaColor8 color);
    void setChanAmbColor(GpuChannelID chan, NebulaColor8 color);
    void setTexCoordGen(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, GpuBool normalize, u32 postMtx);
    void setTexLookupMode(GpuTexMapID map, GpuTexWrapMode wrapS, GpuTexWrapMode wrapT, GpuTexFilter minFilter,
                          GpuTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias, GpuBool biasClamp,
                          GpuBool edgeLod, GpuAnisotropy maxAniso);
    void setTexImgPtr(GpuTexMapID map, const void* pImage);
    // A colour-indexed texture's palette: its format and place (a GX TLUT name).
    void setFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color);
    // Changes of some fields of a register, its others as they are (GX's partial setters).
    void setTevColorIn(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d);
    void setTevAlphaIn(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c, GpuTevAlphaArg d);
    void setTevColorOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out);
    void setTevAlphaOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out);
    void setTevKColorSel(GpuTevStageID stage, GpuTevKColorSel sel);
    void setTevKAlphaSel(GpuTevStageID stage, GpuTevKAlphaSel sel);
    void setTevSwapMode(GpuTevStageID stage, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap);
    void setTevStageOrder(GpuTevStageID stage, GpuTexCoordID coord, GpuTexMapID map, GpuChannelID color);
    void setIndTexOrder(GpuIndTexStageID stage, GpuTexCoordID coord, GpuTexMapID map);
    void enableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points);
    void setColorUpdate(GpuBool enable);
    void setAlphaUpdate(GpuBool enable);
    void setDither(GpuBool enable);
    void setTevKColor(GpuTevKColorID id, NebulaColor8 color);
    void setTevColorS10(GpuTevRegID id, NebulaColor16 color);
    void setClipMode(GpuClipMode mode);
    void setTevSwapModeTable(GpuTevSwapSel table, GpuTevColorChan red, GpuTevColorChan green, GpuTevColorChan blue,
                             GpuTevColorChan alpha);
    // GpuSetTevOp's preset as its four changes (inputs and operations of both halves).
    void setTevOp(GpuTevStageID stage, GpuTevMode mode);
    // GpuSetTexCoordGen2's change: setTexCoordGen's and the coordinate's matrix index.
    void setTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx);
    void setNumTexGens(u8 count);
    void setNumChans(u8 count);
    void setNumTevStages(u8 count);
    void setNumIndStages(u8 count);
    void setCoPlanar(GpuBool enable);
    void setCurrentMtx(u32 id);
    void setTevIndirect(GpuTevStageID stage, GpuIndTexStageID indStage, GpuIndTexFormat format, GpuIndTexBiasSel bias,
                        GpuIndTexMtxID matrix, GpuIndTexWrap wrapS, GpuIndTexWrap wrapT, GpuBool addPrev, GpuBool utcLod,
                        GpuIndTexAlphaSel alpha);
    // GpuSetTevIndWarp's setTevIndirect.
    void setTevIndWarp(GpuTevStageID stage, GpuIndTexStageID indStage, GpuBool signedOffset, GpuBool replaceMode,
                       GpuIndTexMtxID matrix);
    void setIndTexCoordScale(GpuIndTexStageID indStage, GpuIndTexScale scaleS, GpuIndTexScale scaleT);
    void setLineWidth(u8 width, GpuTexOffset texOffsets);
    void setPointSize(u8 size, GpuTexOffset texOffsets);
    // A light (GpuLoadLightObjImm's: its attenuation and colour, then its position and direction).
    void setLight(GpuLightID id, const J3DNativeLight& rLight);
    // Whether the frame keeps alpha; `changed` when it differs from the last value set.
    void setFrameAlpha(bool keepAlpha, bool changed);
    void setZTexture(GpuZTexOp op, GpuTexFmt format, u32 bias);
    // GpuSetFogRangeAdj's changes: an enabled one's table pairs, then the centre and enable.
    void setFogRangeAdj(GpuBool enable, u16 center, const GpuFogAdjTable* pTable);

private:
    NebulaStateOp& add(u8 kind);

    NebulaStateOp* mOps;
    u16 mCount;
    u16 mCapacity;
};
