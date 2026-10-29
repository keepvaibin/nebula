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

void J3DStateList::setTexTlut(GXTexMapID map, GXTlutFmt format, u32 tlutName) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_TLUT);
    r.a[0] = map;
    r.a[1] = format;
    r.a[2] = static_cast< u8 >(tlutName);
}

void J3DStateList::loadTlut(u32 tlutName, const void* pLut) {
    NebulaStateOp& r = add(NEBULA_STATE_TLUT_LOAD);
    r.a[0] = static_cast< u8 >(tlutName);
    r.image = static_cast< u64 >(reinterpret_cast< uintptr_t >(pLut));
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

void J3DStateList::setTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR_IN);
    r.a[0] = stage;
    r.a[1] = a;
    r.a[2] = b;
    r.a[3] = c;
    r.a[4] = d;
}

void J3DStateList::setTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_ALPHA_IN);
    r.a[0] = stage;
    r.a[1] = a;
    r.a[2] = b;
    r.a[3] = c;
    r.a[4] = d;
}

void J3DStateList::setTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp,
                                 GXTevRegID out) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR_OP);
    r.a[0] = stage;
    r.a[1] = op;
    r.a[2] = bias;
    r.a[3] = scale;
    r.a[4] = clamp;
    r.a[5] = out;
}

void J3DStateList::setTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp,
                                 GXTevRegID out) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_ALPHA_OP);
    r.a[0] = stage;
    r.a[1] = op;
    r.a[2] = bias;
    r.a[3] = scale;
    r.a[4] = clamp;
    r.a[5] = out;
}

void J3DStateList::setTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_KCOLOR_SEL);
    r.a[0] = stage;
    r.a[1] = sel;
}

void J3DStateList::setTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_KALPHA_SEL);
    r.a[0] = stage;
    r.a[1] = sel;
}

void J3DStateList::setTevSwapMode(GXTevStageID stage, GXTevSwapSel rasSwap, GXTevSwapSel texSwap) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_SWAP_MODE);
    r.a[0] = stage;
    r.a[1] = rasSwap;
    r.a[2] = texSwap;
}

void J3DStateList::setTevStageOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_STAGE_ORDER);
    r.a[0] = stage;
    r.a[1] = static_cast< u8 >(coord);
    r.a[2] = static_cast< u8 >(map);
    r.a[3] = static_cast< u8 >(color);
    r.a[4] = (map & GX_TEX_DISABLE) != 0 && map != GX_TEXMAP_NULL;
}

void J3DStateList::setColorUpdate(GXBool enable) {
    add(NEBULA_STATE_COLOR_UPDATE).a[0] = enable;
}

void J3DStateList::setAlphaUpdate(GXBool enable) {
    add(NEBULA_STATE_ALPHA_UPDATE).a[0] = enable;
}

void J3DStateList::setDither(GXBool enable) {
    add(NEBULA_STATE_DITHER).a[0] = enable;
}

void J3DStateList::setTevKColor(GXTevKColorID id, GXColor color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_KCOLOR);
    r.a[0] = id;
    setColor(r.a + 1, color);
}

void J3DStateList::setTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green,
                                      GXTevColorChan blue, GXTevColorChan alpha) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_SWAP_TABLE);
    r.a[0] = table;
    r.a[1] = red;
    r.a[2] = green;
    r.a[3] = blue;
    r.a[4] = alpha;
}

void J3DStateList::setTevOp(GXTevStageID stage, GXTevMode mode) {
    // The first stage reads the rasterised colour where later stages read the previous stage's result.
    const bool first = stage == GX_TEVSTAGE0;
    const GXTevColorArg c = first ? GX_CC_RASC : GX_CC_CPREV;
    const GXTevAlphaArg a = first ? GX_CA_RASA : GX_CA_APREV;
    switch (mode) {
    case GX_MODULATE:
        setTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC, c, GX_CC_ZERO);
        setTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA, a, GX_CA_ZERO);
        break;
    case GX_DECAL:
        setTevColorIn(stage, c, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
        setTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, a);
        break;
    case GX_BLEND:
        setTevColorIn(stage, c, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO);
        setTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA, a, GX_CA_ZERO);
        break;
    case GX_REPLACE:
        setTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        setTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
        break;
    default:
        setTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, static_cast< GXTevColorArg >(c));
        setTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, a);
        break;
    }
    setTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    setTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

void J3DStateList::setTexCoordGen2(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize,
                                   u32 postMtx) {
    setTexCoordGen(coord, type, src, normalize, postMtx);
    NebulaStateOp& r = mOps[mCount - 1];
    r.a[5] = 1;
    r.a[6] = static_cast< u8 >(mtx);
}

void J3DStateList::setNumTexGens(u8 count) {
    add(NEBULA_STATE_NUM_TEX_GENS).a[0] = count;
}

void J3DStateList::setNumChans(u8 count) {
    add(NEBULA_STATE_NUM_CHANS).a[0] = count;
}

void J3DStateList::setNumTevStages(u8 count) {
    add(NEBULA_STATE_NUM_TEV_STAGES).a[0] = count;
}

void J3DStateList::setNumIndStages(u8 count) {
    add(NEBULA_STATE_NUM_IND_STAGES).a[0] = count;
}

void J3DStateList::setCoPlanar(GXBool enable) {
    add(NEBULA_STATE_CO_PLANAR).a[0] = enable;
}

void J3DStateList::setZCompLoc(GXBool beforeTex) {
    add(NEBULA_STATE_Z_COMP_LOC).a[0] = beforeTex;
}

void J3DStateList::setCurrentMtx(u32 id) {
    add(NEBULA_STATE_CURRENT_MATRIX).a[0] = static_cast< u8 >(id);
}

void J3DStateList::setTevIndirect(GXTevStageID stage, GXIndTexStageID indStage, GXIndTexFormat format,
                                  GXIndTexBiasSel bias, GXIndTexMtxID matrix, GXIndTexWrap wrapS, GXIndTexWrap wrapT,
                                  GXBool addPrev, GXBool utcLod, GXIndTexAlphaSel alpha) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_INDIRECT);
    r.a[0] = stage;
    r.a[1] = indStage;
    r.a[2] = format;
    r.a[3] = bias;
    r.a[4] = matrix;
    r.a[5] = wrapS;
    r.a[6] = wrapT;
    r.a[7] = addPrev;
    r.a[8] = utcLod;
    r.a[9] = alpha;
}

void J3DStateList::setTevIndWarp(GXTevStageID stage, GXIndTexStageID indStage, GXBool signedOffset, GXBool replaceMode,
                                 GXIndTexMtxID matrix) {
    const GXIndTexWrap wrap = replaceMode ? GX_ITW_0 : GX_ITW_OFF;
    setTevIndirect(stage, indStage, GX_ITF_8, signedOffset ? GX_ITB_STU : GX_ITB_NONE, matrix, wrap, wrap, GX_FALSE,
                   GX_FALSE, GX_ITBA_OFF);
}

void J3DStateList::setIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT) {
    NebulaStateOp& r = add(NEBULA_STATE_IND_TEX_SCALE);
    r.a[0] = indStage;
    r.a[1] = scaleS;
    r.a[2] = scaleT;
}

void J3DStateList::setLineWidth(u8 width, GXTexOffset texOffsets) {
    NebulaStateOp& r = add(NEBULA_STATE_LINE_WIDTH);
    r.a[0] = width;
    r.a[1] = texOffsets;
}

void J3DStateList::setPointSize(u8 size, GXTexOffset texOffsets) {
    NebulaStateOp& r = add(NEBULA_STATE_POINT_SIZE);
    r.a[0] = size;
    r.a[1] = texOffsets;
}

void J3DStateList::setLight(GXLightID id, const J3DNativeLight& rLight) {
    // The light's number: its id's highest bit (GXLoadLightObjImm).
    u8 index = 0;
    for (u32 bits = id; bits > 1; bits >>= 1) {
        index++;
    }
    NebulaStateOp& rAttn = add(NEBULA_STATE_LIGHT_ATTENUATION);
    rAttn.a[0] = index & 7;
    memcpy(rAttn.a + 1, rLight.mColor, 4);
    for (u32 i = 0; i < 3; i++) {
        rAttn.f[i] = rLight.mAngle[i];
        rAttn.f[3 + i] = rLight.mDistance[i];
    }
    NebulaStateOp& rPos = add(NEBULA_STATE_LIGHT_POSITION);
    rPos.a[0] = index & 7;
    for (u32 i = 0; i < 3; i++) {
        rPos.f[i] = rLight.mPosition[i];
        rPos.f[3 + i] = rLight.mDirection[i];
    }
}

void J3DStateList::setPixelFmt(GXPixelFmt format, GXZFmt16 zFormat, bool changed) {
    // GXSetPixelFmt's table of the hardware's pixel types.
    static const u8 sHardwareType[8] = {0, 1, 2, 3, 4, 4, 4, 5};
    NebulaStateOp& r = add(NEBULA_STATE_PIXEL_FORMAT);
    r.a[0] = changed;
    r.a[1] = sHardwareType[format & 7];
    r.a[2] = zFormat;
    r.a[3] = format == GX_PF_RGB565_Z16;
    r.a[4] = sHardwareType[format & 7] == 4;
    r.a[5] = static_cast< u8 >((format - GX_PF_Y8) & 3);
}

void J3DStateList::invalidateTexAll() {
    add(NEBULA_STATE_INVALIDATE_TEXTURES);
}

void J3DStateList::setZTexture(GXZTexOp op, GXTexFmt format, u32 bias) {
    NebulaStateOp& r = add(NEBULA_STATE_Z_TEXTURE);
    r.a[0] = op;
    r.a[1] = format;
    r.a[2] = static_cast< u8 >(bias);
    r.a[3] = static_cast< u8 >(bias >> 8);
    r.a[4] = static_cast< u8 >(bias >> 16);
}

void J3DStateList::setCopyClear(GXColor color, u32 depth) {
    NebulaStateOp& r = add(NEBULA_STATE_COPY_CLEAR);
    setColor(r.a, color);
    r.a[4] = static_cast< u8 >(depth);
    r.a[5] = static_cast< u8 >(depth >> 8);
    r.a[6] = static_cast< u8 >(depth >> 16);
}

void J3DStateList::setCopyFilter(GXBool antialias, const u8 samples[12][2], GXBool verticalFilter,
                                 const u8 coefficients[7]) {
    NebulaStateOp& rSamples = add(NEBULA_STATE_COPY_SAMPLES);
    rSamples.a[0] = antialias;
    if (antialias) {
        for (u32 i = 0; i < 12; i++) {
            rSamples.a[1 + i] = static_cast< u8 >((samples[i][0] & 15) | (samples[i][1] & 15) << 4);
        }
    }
    NebulaStateOp& rFilter = add(NEBULA_STATE_COPY_VFILTER);
    rFilter.a[0] = verticalFilter;
    if (verticalFilter) {
        memcpy(rFilter.a + 1, coefficients, 7);
    }
}

void J3DStateList::setCopyYScale(u32 scale) {
    add(NEBULA_STATE_COPY_YSCALE).size[0] = static_cast< u16 >(scale);
}

void J3DStateList::copy(const NebulaStateOp& rCopy) {
    NebulaStateOp& r = add(NEBULA_STATE_COPY);
    r = rCopy;
    r.kind = NEBULA_STATE_COPY;
}

void J3DStateList::setFogRangeAdj(GXBool enable, u16 center, const GXFogAdjTable* pTable) {
    if (enable) {
        for (u32 i = 0; i < 5; i++) {
            NebulaStateOp& rPair = add(NEBULA_STATE_FOG_RANGE_TABLE);
            rPair.a[0] = static_cast< u8 >(i);
            rPair.size[0] = pTable->r[2 * i];
            rPair.size[1] = pTable->r[2 * i + 1];
        }
    }
    NebulaStateOp& r = add(NEBULA_STATE_FOG_RANGE);
    r.a[0] = enable;
    r.size[0] = center;
}

void J3DStateList::setIndTexOrder(GXIndTexStageID stage, GXTexCoordID coord, GXTexMapID map) {
    NebulaStateOp& r = add(NEBULA_STATE_IND_TEX_ORDER);
    r.a[0] = stage;
    r.a[1] = static_cast< u8 >(coord);
    r.a[2] = static_cast< u8 >(map);
}

void J3DStateList::enableTexOffsets(GXTexCoordID coord, GXBool lines, GXBool points) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_OFFSETS);
    r.a[0] = coord;
    r.a[1] = lines;
    r.a[2] = points;
}

void J3DStateList::setClipMode(GXClipMode mode) {
    add(NEBULA_STATE_CLIP_MODE).a[0] = mode;
}

void J3DStateList::setTevColorS10(GXTevRegID id, GXColorS10 color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR_S10);
    r.a[0] = id;
    r.size[0] = static_cast< u16 >(color.r);
    r.size[1] = static_cast< u16 >(color.g);
    r.f[0] = color.b;
    r.f[1] = color.a;
}
