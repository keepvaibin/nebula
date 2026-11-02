#include <JSystem/J3DGraphBase/J3DNativeChannel.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexGen.hpp>
#include "JSystem/J3DGraphBase/J3DStateList.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <string.h>
#include "JSystem/JUtility/JUTException.hpp"

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    void setColor(u8* pOut, const NebulaColor8& rColor) {
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
    mMaterialPass = rOther.mMaterialPass;
}

namespace {
    // A change the native material folds into itself (its TEV, indirect, alpha-compare and Z-texture state): the
    // host draws with the material's own shader and never receives it.
    bool materialOnly(u8 kind) {
        switch (kind) {
        case NEBULA_STATE_ALPHA_COMPARE:
        case NEBULA_STATE_TEV_COLOR_CALC:
        case NEBULA_STATE_TEV_ALPHA_CALC:
        case NEBULA_STATE_TEV_DIRECT:
        case NEBULA_STATE_TEV_ORDER:
        case NEBULA_STATE_TEV_COLOR:
        case NEBULA_STATE_TEV_COLOR_IN:
        case NEBULA_STATE_TEV_ALPHA_IN:
        case NEBULA_STATE_TEV_COLOR_OP:
        case NEBULA_STATE_TEV_ALPHA_OP:
        case NEBULA_STATE_TEV_KCOLOR_SEL:
        case NEBULA_STATE_TEV_KALPHA_SEL:
        case NEBULA_STATE_TEV_SWAP_MODE:
        case NEBULA_STATE_TEV_STAGE_ORDER:
        case NEBULA_STATE_TEV_KCOLOR:
        case NEBULA_STATE_TEV_COLOR_S10:
        case NEBULA_STATE_IND_TEX_MATRIX:
        case NEBULA_STATE_TEV_SWAP_TABLE:
        case NEBULA_STATE_TEV_INDIRECT:
        case NEBULA_STATE_IND_TEX_SCALE:
        case NEBULA_STATE_IND_TEX_ORDER:
        case NEBULA_STATE_Z_TEXTURE:
        case NEBULA_STATE_FOG_RANGE:
        case NEBULA_STATE_FOG_RANGE_TABLE:
            return true;
        default:
            return false;
        }
    }
}  // namespace

void J3DStateList::send() const {
    if (isEmpty()) {
        return;
    }
    J3DNativeShape::flushState();
    J3DNativeMaterial::state(mOps, mCount);
    if (mMaterialPass != 0) { J3DNativeMaterial::applyPass(mMaterialPass); }
    // The host's share, in order, in runs between material-only changes.
    const NebulaHostServices* pHost = nebula_host();
    u16 start = 0;
    for (u16 i = 0; i <= mCount; i++) {
        if (i == mCount || materialOnly(mOps[i].kind)) {
            if (i > start) {
                pHost->state(pHost->user, mOps + start, i - start);
            }
            start = i + 1;
        }
    }
}

NebulaStateOp& J3DStateList::add(u8 kind) {
    if (mCount == mCapacity) {
        JUTException::panic_f(__FILE__, __LINE__, "J3DStateList: more changes than its storage holds");
    }
    NebulaStateOp& rOp = mOps[mCount++];
    memset(&rOp, 0, sizeof(rOp));
    rOp.kind = kind;
    return rOp;
}

namespace {
    // GpuBlendFactor as the renderer's factor (NEBULA_FACTOR_*): GX's colour factors name the other side's colour (a
    // source factor 2 is the destination's colour).
    u8 nativeFactor(u8 gx, bool source) {
        if (gx == 2 || gx == 3) {
            return source ? static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_DST_COLOR : NEBULA_FACTOR_INV_DST_COLOR)
                          : static_cast< u8 >(gx == 2 ? NEBULA_FACTOR_SRC_COLOR : NEBULA_FACTOR_INV_SRC_COLOR);
        }
        return gx;
    }
    // GpuBlendMode as the blend the renderer performs (NEBULA_BLEND_*).
    u8 nativeBlendOp(GpuBlendMode type) {
        return type == GPU_BM_BLEND ? NEBULA_BLEND_ADD : type == GPU_BM_SUBTRACT ? NEBULA_BLEND_SUBTRACT
           : type == GPU_BM_LOGIC ? NEBULA_BLEND_LOGIC : NEBULA_BLEND_REPLACE;
    }
}  // namespace

void J3DStateList::setBlendModeEtc(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op,
                                   GpuBool colorUpdate, GpuBool alphaUpdate, GpuBool dither) {
    NebulaStateOp& r = add(NEBULA_STATE_BLEND_ETC);
    r.a[0] = nativeBlendOp(type);
    r.a[1] = nativeFactor(src, true);
    r.a[2] = nativeFactor(dst, false);
    r.a[3] = op;
    r.a[4] = colorUpdate;
    r.a[5] = alphaUpdate;
    r.a[6] = dither;
}

void J3DStateList::setBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op) {
    NebulaStateOp& r = add(NEBULA_STATE_BLEND);
    r.a[0] = nativeBlendOp(type);
    r.a[1] = nativeFactor(src, true);
    r.a[2] = nativeFactor(dst, false);
    r.a[3] = op;
}

void J3DStateList::setZMode(GpuBool compare, GpuCompare func, GpuBool update) {
    NebulaStateOp& r = add(NEBULA_STATE_Z_MODE);
    r.a[0] = compare;
    r.a[1] = func;
    r.a[2] = update;
}

void J3DStateList::setAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1) {
    NebulaStateOp& r = add(NEBULA_STATE_ALPHA_COMPARE);
    r.a[0] = comp0;
    r.a[1] = ref0;
    r.a[2] = op;
    r.a[3] = comp1;
    r.a[4] = ref1;
}

void J3DStateList::setDstAlpha(GpuBool enable, u8 alpha) {
    NebulaStateOp& r = add(NEBULA_STATE_DST_ALPHA);
    r.a[0] = enable;
    r.a[1] = alpha;
}

namespace {
    // GpuCullMode as the renderer's (NEBULA_CULL_*: the game's own winding).
    u8 nativeCull(GpuCullMode mode) {
        return mode == GPU_CULL_BACK ? NEBULA_CULL_BACK : mode == GPU_CULL_FRONT ? NEBULA_CULL_FRONT
           : mode == GPU_CULL_ALL ? NEBULA_CULL_ALL : NEBULA_CULL_NONE;
    }
}  // namespace

void J3DStateList::setCullMode(GpuCullMode mode) {
    add(NEBULA_STATE_CULL).a[0] = nativeCull(mode);
}

void J3DStateList::setGenMode(u8 texGens, u8 chans, u8 tevs) {
    NebulaStateOp& r = add(NEBULA_STATE_GEN_MODE);
    r.a[0] = texGens;
    r.a[1] = chans;
    r.a[2] = tevs;
}

void J3DStateList::setGenMode2(u8 texGens, u8 chans, u8 tevs, u8 inds, GpuCullMode cull) {
    NebulaStateOp& r = add(NEBULA_STATE_GEN_MODE2);
    r.a[0] = texGens;
    r.a[1] = chans;
    r.a[2] = tevs;
    r.a[3] = inds;
    r.a[4] = nativeCull(cull);
}

void J3DStateList::setChanCtrl(GpuChannelID chan, GpuBool enable, GpuColorSrc ambSrc, GpuColorSrc matSrc, u32 lightMask,
                               GpuDiffuseFn diffFn, GpuAttnFn attnFn) {
    NebulaStateOp& r = add(NEBULA_STATE_CHANNEL);
    // The channel (0 or 1) and what it sets: 1 its colour, 2 its alpha, 3 both (GPU_COLOR0A0, GPU_COLOR1A1).
    r.a[0] = static_cast< u8 >(chan & 1);
    r.a[7] = chan < GPU_ALPHA0 ? 1 : chan < GPU_COLOR0A0 ? 2 : chan <= GPU_COLOR1A1 ? 3 : 0;
    r.a[1] = enable;
    r.a[2] = ambSrc;
    r.a[3] = matSrc;
    r.a[4] = static_cast< u8 >(lightMask);
    r.a[5] = J3DNativeChannel::diffuse(diffFn, attnFn);
    r.a[6] = J3DNativeChannel::attenuation(attnFn);
}

void J3DStateList::setTevColorCalc(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c,
                                   GpuTevColorArg d, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp,
                                   GpuTevRegID out) {
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

void J3DStateList::setTevAlphaCalcAndSwap(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c,
                                          GpuTevAlphaArg d, GpuTevOp op, GpuTevBias bias, GpuTevScale scale,
                                          GpuBool clamp, GpuTevRegID out, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap) {
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

void J3DStateList::setTevDirect(GpuTevStageID stage) {
    add(NEBULA_STATE_TEV_DIRECT).a[0] = stage;
}

void J3DStateList::setTevOrder(GpuTevStageID evenStage, GpuTexCoordID coord0, GpuTexMapID map0, GpuChannelID color0,
                               GpuTexCoordID coord1, GpuTexMapID map1, GpuChannelID color1) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_ORDER);
    r.a[0] = evenStage;
    r.a[1] = coord0;
    r.a[2] = map0;
    r.a[3] = color0;
    r.a[4] = coord1;
    r.a[5] = map1;
    r.a[6] = color1;
}

void J3DStateList::setTevColor(GpuTevRegID reg, NebulaColor8 color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR);
    r.a[0] = reg;
    setColor(r.a + 1, color);
}

void J3DStateList::setChanMatColor(GpuChannelID chan, NebulaColor8 color) {
    NebulaStateOp& r = add(NEBULA_STATE_MATERIAL_COLOR);
    r.a[0] = static_cast< u8 >(chan & 1);  // the channel (0 or 1); the whole colour
    setColor(r.a + 1, color);
}

void J3DStateList::setChanAmbColor(GpuChannelID chan, NebulaColor8 color) {
    NebulaStateOp& r = add(NEBULA_STATE_AMBIENT_COLOR);
    r.a[0] = static_cast< u8 >(chan & 1);  // the channel (0 or 1); the whole colour
    setColor(r.a + 1, color);
}

void J3DStateList::setTexCoordGen(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, GpuBool normalize,
                                  u32 postMtx) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_COORD_GEN);
    r.a[0] = coord;
    r.a[1] = J3DNativeTexGen::kind(type, src);
    r.a[2] = J3DNativeTexGen::source(src);
    r.a[3] = normalize;
    r.a[4] = J3DNativeTexGen::post(postMtx);
}

void J3DStateList::setTexLookupMode(GpuTexMapID map, GpuTexWrapMode wrapS, GpuTexWrapMode wrapT, GpuTexFilter minFilter,
                                    GpuTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias, GpuBool biasClamp,
                                    GpuBool edgeLod, GpuAnisotropy maxAniso) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_LOOKUP);
    r.a[0] = map;
    r.a[1] = wrapS;
    r.a[2] = wrapT;
    r.a[3] = minFilter == GPU_LINEAR || minFilter == GPU_LIN_MIP_NEAR || minFilter == GPU_LIN_MIP_LIN;
    r.a[4] = magFilter != GPU_NEAR;
    r.a[5] = minFilter < GPU_NEAR_MIP_NEAR ? 0 : minFilter < GPU_NEAR_MIP_LIN ? 1 : 2;
    r.a[6] = biasClamp;
    r.a[7] = edgeLod;
    r.a[8] = maxAniso;
    r.f[0] = minLod;
    r.f[1] = maxLod;
    r.f[2] = lodBias;
}

void J3DStateList::setTexImgPtr(GpuTexMapID map, const void* pImage) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_IMAGE);
    r.a[0] = map;
    r.image = static_cast< u64 >(reinterpret_cast< uintptr_t >(pImage));
}

void J3DStateList::setFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color) {
    NebulaStateOp& r = add(NEBULA_STATE_FOG);
    r.a[0] = type;
    setColor(r.a + 1, color);
    r.f[0] = start;
    r.f[1] = end;
    r.f[2] = nearZ;
    r.f[3] = farZ;
}

void J3DStateList::setTevColorIn(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR_IN);
    r.a[0] = stage;
    r.a[1] = a;
    r.a[2] = b;
    r.a[3] = c;
    r.a[4] = d;
}

void J3DStateList::setTevAlphaIn(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c, GpuTevAlphaArg d) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_ALPHA_IN);
    r.a[0] = stage;
    r.a[1] = a;
    r.a[2] = b;
    r.a[3] = c;
    r.a[4] = d;
}

void J3DStateList::setTevColorOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp,
                                 GpuTevRegID out) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR_OP);
    r.a[0] = stage;
    r.a[1] = op;
    r.a[2] = bias;
    r.a[3] = scale;
    r.a[4] = clamp;
    r.a[5] = out;
}

void J3DStateList::setTevAlphaOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp,
                                 GpuTevRegID out) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_ALPHA_OP);
    r.a[0] = stage;
    r.a[1] = op;
    r.a[2] = bias;
    r.a[3] = scale;
    r.a[4] = clamp;
    r.a[5] = out;
}

void J3DStateList::setTevKColorSel(GpuTevStageID stage, GpuTevKColorSel sel) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_KCOLOR_SEL);
    r.a[0] = stage;
    r.a[1] = sel;
}

void J3DStateList::setTevKAlphaSel(GpuTevStageID stage, GpuTevKAlphaSel sel) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_KALPHA_SEL);
    r.a[0] = stage;
    r.a[1] = sel;
}

void J3DStateList::setTevSwapMode(GpuTevStageID stage, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_SWAP_MODE);
    r.a[0] = stage;
    r.a[1] = rasSwap;
    r.a[2] = texSwap;
}

void J3DStateList::setTevStageOrder(GpuTevStageID stage, GpuTexCoordID coord, GpuTexMapID map, GpuChannelID color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_STAGE_ORDER);
    r.a[0] = stage;
    r.a[1] = static_cast< u8 >(coord);
    r.a[2] = static_cast< u8 >(map);
    r.a[3] = static_cast< u8 >(color);
    r.a[4] = (map & GPU_TEX_DISABLE) != 0 && map != GPU_TEXMAP_NULL;
}

void J3DStateList::setColorUpdate(GpuBool enable) {
    add(NEBULA_STATE_COLOR_UPDATE).a[0] = enable;
}

void J3DStateList::setAlphaUpdate(GpuBool enable) {
    add(NEBULA_STATE_ALPHA_UPDATE).a[0] = enable;
}

void J3DStateList::setDither(GpuBool enable) {
    add(NEBULA_STATE_DITHER).a[0] = enable;
}

void J3DStateList::setTevKColor(GpuTevKColorID id, NebulaColor8 color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_KCOLOR);
    r.a[0] = id;
    setColor(r.a + 1, color);
}

void J3DStateList::setTevSwapModeTable(GpuTevSwapSel table, GpuTevColorChan red, GpuTevColorChan green,
                                      GpuTevColorChan blue, GpuTevColorChan alpha) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_SWAP_TABLE);
    r.a[0] = table;
    r.a[1] = red;
    r.a[2] = green;
    r.a[3] = blue;
    r.a[4] = alpha;
}

void J3DStateList::setTevOp(GpuTevStageID stage, GpuTevMode mode) {
    // The first stage reads the rasterised colour where later stages read the previous stage's result.
    const bool first = stage == GPU_TEVSTAGE0;
    const GpuTevColorArg c = first ? GPU_CC_RASC : GPU_CC_CPREV;
    const GpuTevAlphaArg a = first ? GPU_CA_RASA : GPU_CA_APREV;
    switch (mode) {
    case GPU_MODULATE:
        setTevColorIn(stage, GPU_CC_ZERO, GPU_CC_TEXC, c, GPU_CC_ZERO);
        setTevAlphaIn(stage, GPU_CA_ZERO, GPU_CA_TEXA, a, GPU_CA_ZERO);
        break;
    case GPU_DECAL:
        setTevColorIn(stage, c, GPU_CC_TEXC, GPU_CC_TEXA, GPU_CC_ZERO);
        setTevAlphaIn(stage, GPU_CA_ZERO, GPU_CA_ZERO, GPU_CA_ZERO, a);
        break;
    case GPU_BLEND:
        setTevColorIn(stage, c, GPU_CC_ONE, GPU_CC_TEXC, GPU_CC_ZERO);
        setTevAlphaIn(stage, GPU_CA_ZERO, GPU_CA_TEXA, a, GPU_CA_ZERO);
        break;
    case GPU_REPLACE:
        setTevColorIn(stage, GPU_CC_ZERO, GPU_CC_ZERO, GPU_CC_ZERO, GPU_CC_TEXC);
        setTevAlphaIn(stage, GPU_CA_ZERO, GPU_CA_ZERO, GPU_CA_ZERO, GPU_CA_TEXA);
        break;
    default:
        setTevColorIn(stage, GPU_CC_ZERO, GPU_CC_ZERO, GPU_CC_ZERO, static_cast< GpuTevColorArg >(c));
        setTevAlphaIn(stage, GPU_CA_ZERO, GPU_CA_ZERO, GPU_CA_ZERO, a);
        break;
    }
    setTevColorOp(stage, GPU_TEV_ADD, GPU_TB_ZERO, GPU_CS_SCALE_1, GPU_TRUE, GPU_TEVPREV);
    setTevAlphaOp(stage, GPU_TEV_ADD, GPU_TB_ZERO, GPU_CS_SCALE_1, GPU_TRUE, GPU_TEVPREV);
}

void J3DStateList::setTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize,
                                   u32 postMtx) {
    setTexCoordGen(coord, type, src, normalize, postMtx);
    NebulaStateOp& r = mOps[mCount - 1];
    r.a[5] = 1;
    // GX's matrix id (three rows a matrix: GPU_PNMTX0-9 0-27, GPU_TEXMTX0-9 30-57, GPU_IDENTITY 60) as its slot.
    r.a[6] = static_cast< u8 >(mtx / 3);
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

void J3DStateList::setCoPlanar(GpuBool enable) {
    add(NEBULA_STATE_CO_PLANAR).a[0] = enable;
}

void J3DStateList::setCurrentMtx(u32 id) {
    add(NEBULA_STATE_CURRENT_MATRIX).a[0] = static_cast< u8 >(id / 3);  // GX_PNMTXn is 3n
}

void J3DStateList::setTevIndirect(GpuTevStageID stage, GpuIndTexStageID indStage, GpuIndTexFormat format,
                                  GpuIndTexBiasSel bias, GpuIndTexMtxID matrix, GpuIndTexWrap wrapS, GpuIndTexWrap wrapT,
                                  GpuBool addPrev, GpuBool utcLod, GpuIndTexAlphaSel alpha) {
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

void J3DStateList::setTevIndWarp(GpuTevStageID stage, GpuIndTexStageID indStage, GpuBool signedOffset, GpuBool replaceMode,
                                 GpuIndTexMtxID matrix) {
    const GpuIndTexWrap wrap = replaceMode ? GPU_ITW_0 : GPU_ITW_OFF;
    setTevIndirect(stage, indStage, GPU_ITF_8, signedOffset ? GPU_ITB_STU : GPU_ITB_NONE, matrix, wrap, wrap, GPU_FALSE,
                   GPU_FALSE, GPU_ITBA_OFF);
}

void J3DStateList::setIndTexCoordScale(GpuIndTexStageID indStage, GpuIndTexScale scaleS, GpuIndTexScale scaleT) {
    NebulaStateOp& r = add(NEBULA_STATE_IND_TEX_SCALE);
    r.a[0] = indStage;
    r.a[1] = scaleS;
    r.a[2] = scaleT;
}

namespace {
    // GpuTexOffset to the n of a 1/n-texel coordinate offset (0: none; 1/16 to 1, then 1).
    const u8 sTexOffsetDivisor[8] = {0, 16, 8, 4, 2, 1, 1, 1};
}  // namespace

// GX's widths are sixths of a pixel: the op carries pixels.
void J3DStateList::setLineWidth(u8 width, GpuTexOffset texOffsets) {
    NebulaStateOp& r = add(NEBULA_STATE_LINE_WIDTH);
    r.f[0] = width / 6.0f;
    r.a[1] = sTexOffsetDivisor[texOffsets & 7];
}

void J3DStateList::setPointSize(u8 size, GpuTexOffset texOffsets) {
    NebulaStateOp& r = add(NEBULA_STATE_POINT_SIZE);
    r.f[0] = size / 6.0f;
    r.a[1] = sTexOffsetDivisor[texOffsets & 7];
}

void J3DStateList::setLight(GpuLightID id, const J3DNativeLight& rLight) {
    // The light's number: its id's highest bit (GpuLoadLightObjImm).
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

void J3DStateList::setFrameAlpha(bool keepAlpha, bool changed) {
    NebulaStateOp& r = add(NEBULA_STATE_FRAME_ALPHA);
    r.a[0] = changed;
    r.a[1] = keepAlpha;
}

void J3DStateList::setZTexture(GpuZTexOp op, GpuTexFmt format, u32 bias) {
    NebulaStateOp& r = add(NEBULA_STATE_Z_TEXTURE);
    r.a[0] = op;
    r.a[1] = format;
    r.a[2] = static_cast< u8 >(bias);
    r.a[3] = static_cast< u8 >(bias >> 8);
    r.a[4] = static_cast< u8 >(bias >> 16);
}

void J3DStateList::setFogRangeAdj(GpuBool enable, u16 center, const GpuFogAdjTable* pTable) {
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

void J3DStateList::setIndTexOrder(GpuIndTexStageID stage, GpuTexCoordID coord, GpuTexMapID map) {
    NebulaStateOp& r = add(NEBULA_STATE_IND_TEX_ORDER);
    r.a[0] = stage;
    r.a[1] = static_cast< u8 >(coord);
    r.a[2] = static_cast< u8 >(map);
}

void J3DStateList::enableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points) {
    NebulaStateOp& r = add(NEBULA_STATE_TEX_OFFSETS);
    r.a[0] = coord;
    r.a[1] = lines;
    r.a[2] = points;
}

void J3DStateList::setClipMode(GpuClipMode mode) {
    add(NEBULA_STATE_CLIP_MODE).a[0] = mode;
}

void J3DStateList::setTevColorS10(GpuTevRegID id, NebulaColor16 color) {
    NebulaStateOp& r = add(NEBULA_STATE_TEV_COLOR_S10);
    r.a[0] = id;
    r.size[0] = static_cast< u16 >(color.r);
    r.size[1] = static_cast< u16 >(color.g);
    r.f[0] = color.b;
    r.f[1] = color.a;
}
