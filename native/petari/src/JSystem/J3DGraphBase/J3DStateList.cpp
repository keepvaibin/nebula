#include "JSystem/J3DGraphBase/J3DStateList.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <nebula_gx_fifo.h>
#include <revolution/gx/GXRegs.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    void setColor(u8* pOut, const GXColor& rColor) {
        pOut[0] = rColor.r;
        pOut[1] = rColor.g;
        pOut[2] = rColor.b;
        pOut[3] = rColor.a;
    }
}  // namespace

void J3DStateList::reserve(u16 capacity) {
    mOps = new NebulaStateOp[capacity];
    mCount = 0;
    mCapacity = capacity;
}

void J3DStateList::copy(const J3DStateList& rOther) {
    if (mCapacity < rOther.mCount) {
        reserve(rOther.mCount);
    }
    if (rOther.mCount != 0) {
        memcpy(mOps, rOther.mOps, sizeof(NebulaStateOp) * rOther.mCount);
    }
    mCount = rOther.mCount;
}

void J3DStateList::send() const {
    if (mCount == 0) {
        return;
    }
    J3DNativeShape::flushState();
    // The drawing state the game set through GX's functions, written before the record (as a GX call would).
    if (gx->dirtyState) {
        __GXSetDirtyState();
    }
    const NebulaHostServices* pHost = nebula_host();
    pHost->state(pHost->user, __NebulaGXWriter.ptr, mOps, mCount);
}

NebulaStateOp& J3DStateList::add(u8 kind) {
    if (mCount == mCapacity) {
        OSPanic(__FILE__, __LINE__, "J3DStateList: more changes than its storage holds");
    }
    NebulaStateOp& rOp = mOps[mCount++];
    memset(&rOp, 0, sizeof(rOp));
    rOp.kind = kind;
    return rOp;
}

void J3DStateList::setBlendModeEtc(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op,
                                   GXBool colorUpdate, GXBool alphaUpdate, GXBool dither) {
    NebulaStateOp& r = add(NEBULA_STATE_BLEND_ETC);
    r.a[0] = type;
    r.a[1] = src;
    r.a[2] = dst;
    r.a[3] = op;
    r.a[4] = colorUpdate;
    r.a[5] = alphaUpdate;
    r.a[6] = dither;
}

void J3DStateList::setBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op) {
    NebulaStateOp& r = add(NEBULA_STATE_BLEND);
    r.a[0] = type;
    r.a[1] = src;
    r.a[2] = dst;
    r.a[3] = op;
}

void J3DStateList::setZMode(GXBool compare, GXCompare func, GXBool update) {
    NebulaStateOp& r = add(NEBULA_STATE_Z_MODE);
    r.a[0] = compare;
    r.a[1] = func;
    r.a[2] = update;
}

void J3DStateList::setAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
    NebulaStateOp& r = add(NEBULA_STATE_ALPHA_COMPARE);
    r.a[0] = comp0;
    r.a[1] = ref0;
    r.a[2] = op;
    r.a[3] = comp1;
    r.a[4] = ref1;
}

void J3DStateList::setDstAlpha(GXBool enable, u8 alpha) {
    NebulaStateOp& r = add(NEBULA_STATE_DST_ALPHA);
    r.a[0] = enable;
    r.a[1] = alpha;
}

void J3DStateList::setCullMode(GXCullMode mode) {
    add(NEBULA_STATE_CULL).a[0] = mode;
}

void J3DStateList::setGenMode(u8 texGens, u8 chans, u8 tevs) {
    NebulaStateOp& r = add(NEBULA_STATE_GEN_MODE);
    r.a[0] = texGens;
    r.a[1] = chans;
    r.a[2] = tevs;
}

void J3DStateList::setGenMode2(u8 texGens, u8 chans, u8 tevs, u8 inds, GXCullMode cull) {
    NebulaStateOp& r = add(NEBULA_STATE_GEN_MODE2);
    r.a[0] = texGens;
    r.a[1] = chans;
    r.a[2] = tevs;
    r.a[3] = inds;
    r.a[4] = cull;
}

void J3DStateList::setChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask,
                               GXDiffuseFn diffFn, GXAttnFn attnFn) {
    NebulaStateOp& r = add(NEBULA_STATE_CHANNEL);
    r.a[0] = chan;
    r.a[1] = enable;
    r.a[2] = ambSrc;
    r.a[3] = matSrc;
    r.a[4] = static_cast< u8 >(lightMask);
    r.a[5] = diffFn;
    r.a[6] = attnFn;
}

void J3DStateList::setTevColorCalc(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c,
                                   GXTevColorArg d, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp,
                                   GXTevRegID out) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR_CALC);
    r.a[0] = stage;
    r.a[1] = a;
    r.a[2] = b;
    r.a[3] = c;
    r.a[4] = d;
    r.a[5] = op;
    r.a[6] = bias;
    r.a[7] = scale;
    r.a[8] = clamp;
    r.a[9] = out;
}

void J3DStateList::setTevAlphaCalcAndSwap(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c,
                                          GXTevAlphaArg d, GXTevOp op, GXTevBias bias, GXTevScale scale,
                                          GXBool clamp, GXTevRegID out, GXTevSwapSel rasSwap, GXTevSwapSel texSwap) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_ALPHA_CALC);
    r.a[0] = stage;
    r.a[1] = a;
    r.a[2] = b;
    r.a[3] = c;
    r.a[4] = d;
    r.a[5] = op;
    r.a[6] = bias;
    r.a[7] = scale;
    r.a[8] = clamp;
    r.a[9] = out;
    r.a[10] = rasSwap;
    r.a[11] = texSwap;
}

void J3DStateList::setTevDirect(GXTevStageID stage) {
    add(NEBULA_STATE_TEV_DIRECT).a[0] = stage;
}

void J3DStateList::setTevOrder(GXTevStageID evenStage, GXTexCoordID coord0, GXTexMapID map0, GXChannelID color0,
                               GXTexCoordID coord1, GXTexMapID map1, GXChannelID color1) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_ORDER);
    r.a[0] = evenStage;
    r.a[1] = coord0;
    r.a[2] = map0;
    r.a[3] = color0;
    r.a[4] = coord1;
    r.a[5] = map1;
    r.a[6] = color1;
}

void J3DStateList::setTevColor(GXTevRegID reg, GXColor color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR);
    r.a[0] = reg;
    setColor(r.a + 1, color);
}

void J3DStateList::setChanMatColor(GXChannelID chan, GXColor color) {
    NebulaStateOp& r = add(NEBULA_STATE_MATERIAL_COLOR);
    r.a[0] = chan;
    setColor(r.a + 1, color);
}

void J3DStateList::setChanAmbColor(GXChannelID chan, GXColor color) {
    NebulaStateOp& r = add(NEBULA_STATE_AMBIENT_COLOR);
    r.a[0] = chan;
    setColor(r.a + 1, color);
}

void J3DStateList::setTexCoordGen(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, GXBool normalize,
                                  u32 postMtx) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_COORD_GEN);
    r.a[0] = coord;
    r.a[1] = type;
    r.a[2] = src;
    r.a[3] = normalize;
    r.a[4] = static_cast< u8 >(postMtx);
}

void J3DStateList::setTexLookupMode(GXTexMapID map, GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXTexFilter minFilter,
                                    GXTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias, GXBool biasClamp,
                                    GXBool edgeLod, GXAnisotropy maxAniso) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_LOOKUP);
    r.a[0] = map;
    r.a[1] = wrapS;
    r.a[2] = wrapT;
    r.a[3] = minFilter;
    r.a[4] = magFilter;
    r.a[5] = biasClamp;
    r.a[6] = edgeLod;
    r.a[7] = maxAniso;
    r.f[0] = minLod;
    r.f[1] = maxLod;
    r.f[2] = lodBias;
}

void J3DStateList::setTexImgAttr(GXTexMapID map, u16 width, u16 height, GXTexFmt format) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_IMAGE_ATTR);
    r.a[0] = map;
    r.a[1] = format;
    r.size[0] = width;
    r.size[1] = height;
}

void J3DStateList::setTexImgPtr(GXTexMapID map, const void* pImage) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_IMAGE);
    r.a[0] = map;
    r.image = static_cast< u64 >(reinterpret_cast< uintptr_t >(pImage));
}

void J3DStateList::setFog(GXFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, GXColor color) {
    NebulaStateOp& r = add(NEBULA_STATE_FOG);
    r.a[0] = type;
    setColor(r.a + 1, color);
    r.f[0] = start;
    r.f[1] = end;
    r.f[2] = nearZ;
    r.f[3] = farZ;
}
