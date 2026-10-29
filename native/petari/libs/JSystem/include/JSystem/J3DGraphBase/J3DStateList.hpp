#pragma once

#include <nebula_game_api.h>
#include <revolution.h>

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
    void setFog(GXFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, GXColor color);

private:
    NebulaStateOp& add(u8 kind);

    NebulaStateOp* mOps;
    u16 mCount;
    u16 mCapacity;
};
