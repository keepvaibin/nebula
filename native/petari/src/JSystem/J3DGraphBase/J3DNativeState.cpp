#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DStateList.hpp"
#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "JSystem/J3DGraphBase/J3DNativeTexture.hpp"
#include "JSystem/J3DGraphBase/J3DNativeView.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <math.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // GpuInit's cull mode and frame (no alpha).
    GpuCullMode sCullMode = GPU_CULL_BACK;
    bool sFrameAlpha = false;

    static_assert(sizeof(J3DNativeLight) <= sizeof(GpuLightObj), "a GpuLightObj holds a J3DNativeLight");

    J3DNativeLight& light(GpuLightObj* pObj) {
        return *reinterpret_cast< J3DNativeLight* >(pObj);
    }
}  // namespace

namespace J3DNativeState {
    void setZMode(GpuBool compare, GpuCompare func, GpuBool update) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setZMode(compare, func, update);
        list.send();
    }

    void setAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp alphaOp, GpuCompare comp1, u8 ref1) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setAlphaCompare(comp0, ref0, alphaOp, comp1, ref1);
        list.send();
    }

    void setDstAlpha(GpuBool enable, u8 alpha) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setDstAlpha(enable, alpha);
        list.send();
    }

    void setTevColor(GpuTevRegID id, NebulaColor8 color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevColor(id, color);
        list.send();
    }

    void setTevKColor(GpuTevKColorID id, NebulaColor8 color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevKColor(id, color);
        list.send();
    }

    void setTevColorS10(GpuTevRegID id, NebulaColor16 color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevColorS10(id, color);
        list.send();
    }

    void setChanCtrl(GpuChannelID chan, GpuBool enable, GpuColorSrc ambSrc, GpuColorSrc matSrc, u32 lightMask,
                     GpuDiffuseFn diffFn, GpuAttnFn attnFn) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setChanCtrl(chan, enable, ambSrc, matSrc, lightMask, diffFn, attnFn);
        list.send();
    }

    void setTevDirect(GpuTevStageID stage) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevDirect(stage);
        list.send();
    }

    void setFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setFog(type, start, end, nearZ, farZ, color);
        op.a[5] = 1;
        list.send();
    }

    void setClipMode(GpuClipMode mode) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setClipMode(mode);
        list.send();
    }

    void setIndTexMtx(GpuIndTexMtxID id, const f32 offset[2][3], s8 scaleExp) {
        NebulaStateOp op;
        memset(&op, 0, sizeof(op));
        op.kind = NEBULA_STATE_IND_TEX_MATRIX;
        op.a[0] = static_cast< u8 >(id);
        op.a[1] = static_cast< u8 >(scaleExp);
        for (u32 c = 0; c < 3; c++) {
            op.f[2 * c] = offset[0][c];
            op.f[2 * c + 1] = offset[1][c];
        }
        J3DStateList list(&op, 1);
        list.adopt(1);
        list.send();
    }

    void setBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setBlendMode(type, src, dst, op);
        list.send();
    }

    void setTevColorIn(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevColorIn(stage, a, b, c, d);
        list.send();
    }

    void setTevAlphaIn(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c, GpuTevAlphaArg d) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevAlphaIn(stage, a, b, c, d);
        list.send();
    }

    void setTevColorOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevColorOp(stage, op, bias, scale, clamp, out);
        list.send();
    }

    void setTevAlphaOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevAlphaOp(stage, op, bias, scale, clamp, out);
        list.send();
    }

    void setTevKColorSel(GpuTevStageID stage, GpuTevKColorSel sel) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevKColorSel(stage, sel);
        list.send();
    }

    void setTevKAlphaSel(GpuTevStageID stage, GpuTevKAlphaSel sel) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevKAlphaSel(stage, sel);
        list.send();
    }

    void setTevSwapMode(GpuTevStageID stage, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevSwapMode(stage, rasSwap, texSwap);
        list.send();
    }

    void setTevSwapModeTable(GpuTevSwapSel table, GpuTevColorChan red, GpuTevColorChan green, GpuTevColorChan blue, GpuTevColorChan alpha) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevSwapModeTable(table, red, green, blue, alpha);
        list.send();
    }

    void setColorUpdate(GpuBool enable) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setColorUpdate(enable);
        list.send();
    }

    void setAlphaUpdate(GpuBool enable) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setAlphaUpdate(enable);
        list.send();
    }

    void setDither(GpuBool enable) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setDither(enable);
        list.send();
    }

    void setTevOp(GpuTevStageID stage, GpuTevMode mode) {
        NebulaStateOp ops[4];
        J3DStateList list(ops, 4);
        list.setTevOp(stage, mode);
        list.send();
    }

    void setChanMatColor(GpuChannelID chan, NebulaColor8 color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setChanMatColor(chan, color);
        op.a[5] = 1;
        list.send();
    }

    void setChanAmbColor(GpuChannelID chan, NebulaColor8 color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setChanAmbColor(chan, color);
        op.a[5] = 1;
        list.send();
    }

    void setTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTexCoordGen2(coord, type, src, mtx, normalize, postMtx);
        list.send();
    }

    void setNumTexGens(u8 count) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setNumTexGens(count);
        list.send();
    }

    void setNumChans(u8 count) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setNumChans(count);
        list.send();
    }

    void setNumTevStages(u8 count) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setNumTevStages(count);
        list.send();
    }

    void setNumIndStages(u8 count) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setNumIndStages(count);
        list.send();
    }

    void setCoPlanar(GpuBool enable) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCoPlanar(enable);
        list.send();
    }

    void setCurrentMtx(u32 id) {
        J3DNativeShape::setCurrentPosition(id / 3);
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCurrentMtx(id);
        list.send();
    }

    void setTevIndirect(GpuTevStageID stage, GpuIndTexStageID indStage, GpuIndTexFormat format, GpuIndTexBiasSel bias, GpuIndTexMtxID matrix, GpuIndTexWrap wrapS, GpuIndTexWrap wrapT, GpuBool addPrev, GpuBool utcLod, GpuIndTexAlphaSel alpha) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevIndirect(stage, indStage, format, bias, matrix, wrapS, wrapT, addPrev, utcLod, alpha);
        list.send();
    }

    void setTevIndWarp(GpuTevStageID stage, GpuIndTexStageID indStage, GpuBool signedOffset, GpuBool replaceMode, GpuIndTexMtxID matrix) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevIndWarp(stage, indStage, signedOffset, replaceMode, matrix);
        list.send();
    }

    void setIndTexCoordScale(GpuIndTexStageID indStage, GpuIndTexScale scaleS, GpuIndTexScale scaleT) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setIndTexCoordScale(indStage, scaleS, scaleT);
        list.send();
    }

    void setLineWidth(u8 width, GpuTexOffset texOffsets) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setLineWidth(width, texOffsets);
        list.send();
    }

    void setPointSize(u8 size, GpuTexOffset texOffsets) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setPointSize(size, texOffsets);
        list.send();
    }

    void initLightColor(GpuLightObj* pObj, NebulaColor8 color) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mColor[0] = color.r;
        rLight.mColor[1] = color.g;
        rLight.mColor[2] = color.b;
        rLight.mColor[3] = color.a;
    }

    void initLightPos(GpuLightObj* pObj, f32 x, f32 y, f32 z) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mPosition[0] = x;
        rLight.mPosition[1] = y;
        rLight.mPosition[2] = z;
    }

    void initLightDir(GpuLightObj* pObj, f32 nx, f32 ny, f32 nz) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mDirection[0] = -nx;
        rLight.mDirection[1] = -ny;
        rLight.mDirection[2] = -nz;
    }

    // GX's distance for a light at infinity.
    const f32 cInfiniteLightDistance = 1.0e18f;

    void initSpecularDir(GpuLightObj* pObj, f32 nx, f32 ny, f32 nz) {
        const f32 hx = -nx;
        const f32 hy = -ny;
        const f32 hz = 1.0f - nz;
        f32 mag = hx * hx + hy * hy + hz * hz;
        if (mag != 0.0f) {
            mag = 1.0f / sqrtf(mag);
        }
        initSpecularDirHA(pObj, nx, ny, nz, hx * mag, hy * mag, hz * mag);
    }

    void initSpecularDirHA(GpuLightObj* pObj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mDirection[0] = hx;
        rLight.mDirection[1] = hy;
        rLight.mDirection[2] = hz;
        rLight.mPosition[0] = nx * -cInfiniteLightDistance;
        rLight.mPosition[1] = ny * -cInfiniteLightDistance;
        rLight.mPosition[2] = nz * -cInfiniteLightDistance;
    }

    void initLightAttn(GpuLightObj* pObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mAngle[0] = a0;
        rLight.mAngle[1] = a1;
        rLight.mAngle[2] = a2;
        rLight.mDistance[0] = k0;
        rLight.mDistance[1] = k1;
        rLight.mDistance[2] = k2;
    }

    void initLightSpot(GpuLightObj* pObj, f32 cutoff, GpuSpotFn spotFn) {
        if (cutoff <= 0.0f || cutoff > 90.0f) {
            spotFn = GPU_SP_OFF;
        }
        const f32 cr = cos(cutoff * 3.14159265358979323846f / 180.0f);
        f32 a0, a1, a2, d;
        switch (spotFn) {
        case GPU_SP_FLAT:
            a0 = -1000.0f * cr;
            a1 = 1000.0f;
            a2 = 0.0f;
            break;
        case GPU_SP_COS:
            a1 = 1.0f / (1.0f - cr);
            a0 = -cr * a1;
            a2 = 0.0f;
            break;
        case GPU_SP_COS2:
            a2 = 1.0f / (1.0f - cr);
            a0 = 0.0f;
            a1 = -cr * a2;
            break;
        case GPU_SP_SHARP:
            d = 1.0f / ((1.0f - cr) * (1.0f - cr));
            a0 = cr * (cr - 2.0f) * d;
            a1 = 2.0f * d;
            a2 = -d;
            break;
        case GPU_SP_RING1:
            d = 1.0f / ((1.0f - cr) * (1.0f - cr));
            a2 = -4.0f * d;
            a0 = a2 * cr;
            a1 = 4.0f * (1.0f + cr) * d;
            break;
        case GPU_SP_RING2:
            d = 1.0f / ((1.0f - cr) * (1.0f - cr));
            a0 = 1.0f - 2.0f * cr * cr * d;
            a1 = 4.0f * cr * d;
            a2 = -2.0f * d;
            break;
        default:
            a0 = 1.0f;
            a1 = 0.0f;
            a2 = 0.0f;
            break;
        }
        J3DNativeLight& rLight = light(pObj);
        rLight.mAngle[0] = a0;
        rLight.mAngle[1] = a1;
        rLight.mAngle[2] = a2;
    }

    void initLightDistAttn(GpuLightObj* pObj, f32 refDistance, f32 refBrightness, GpuDistAttnFn distFn) {
        if (refDistance < 0.0f || refBrightness <= 0.0f || refBrightness >= 1.0f) {
            distFn = GPU_DA_OFF;
        }
        f32 k0 = 1.0f, k1 = 0.0f, k2 = 0.0f;
        switch (distFn) {
        case GPU_DA_GENTLE:
            k1 = (1.0f - refBrightness) / (refBrightness * refDistance);
            break;
        case GPU_DA_MEDIUM:
            k1 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance);
            k2 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
            break;
        case GPU_DA_STEEP:
            k2 = (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
            break;
        default:
            break;
        }
        J3DNativeLight& rLight = light(pObj);
        rLight.mDistance[0] = k0;
        rLight.mDistance[1] = k1;
        rLight.mDistance[2] = k2;
    }

    void loadLightObjImm(const GpuLightObj* pObj, GpuLightID id) {
        NebulaStateOp ops[2];
        J3DStateList list(ops, 2);
        list.setLight(id, *reinterpret_cast< const J3DNativeLight* >(pObj));
        list.send();
    }

    void setFrameAlpha(bool keepAlpha) {
        const bool changed = keepAlpha != sFrameAlpha;
        sFrameAlpha = keepAlpha;
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setFrameAlpha(keepAlpha, changed);
        list.send();
    }

    // The texture every map holds until the game loads its own: 4x4 opaque white, a texture record
    // (nebula_cooked_texture.h).
    static struct {
        NebulaTexture header;
        u8 texels[4 * 4 * 2];
    } sDefaultTexData ATTRIBUTE_ALIGN(32) = {NEBULA_TEXTURE_STATIC(4, 4, NEBULA_TEXTURE_LA8, 4 * 4 * 2),
                                             {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    // The projection before the game sets its own (orthographic, -1..1 depth).
    static const f32 sDefaultProjection[7] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -2.0f, 0.0f};
    // The frame GpuInit assumed (its NTSC 480-line mode): 640x480.
    const u16 cDefaultWidth = 640;
    const u16 cDefaultHeight = 480;

    void initDefaults() {
        const NebulaColor8 black = {0, 0, 0, 0};
        const NebulaColor8 white = {255, 255, 255, 255};

        for (u32 i = 0; i < 8; i++) {
            setTexCoordGen2(static_cast< GpuTexCoordID >(i), GX_TG_MTX2x4, static_cast< GpuTexGenSrc >(GPU_TG_TEX0 + i),
                            GPU_IDENTITY, GPU_FALSE, GPU_PTIDENTITY);
        }
        setNumTexGens(1);
        setLineWidth(6, GPU_TO_ZERO);
        setPointSize(6, GPU_TO_ZERO);
        for (u32 i = 0; i < 8; i++) {
            enableTexOffsets(static_cast< GpuTexCoordID >(i), GPU_FALSE, GPU_FALSE);
        }

        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DNativeShape::setNormal(0, identity);
        setCurrentMtx(GPU_PNMTX0);
        J3DNativeShape::setTexture(J3DNativeShape::kIdentityTexture, identity, 3);
        J3DNativeShape::setPostTexture(J3DNativeShape::kIdentityPost, identity);
        J3DNativeView::setViewport(0.0f, 0.0f, cDefaultWidth, cDefaultHeight, 0.0f, 1.0f);
        J3DNativeView::setProjectionv(sDefaultProjection);

        setCoPlanar(GPU_FALSE);
        setCullMode(GPU_CULL_BACK);
        setClipMode(GPU_CLIP_ENABLE);
        J3DNativeView::setScissor(0, 0, cDefaultWidth, cDefaultHeight);
        setNumChans(0);
        setChanCtrl(GPU_COLOR0A0, GPU_FALSE, GPU_SRC_REG, GPU_SRC_VTX, GPU_LIGHT_NULL, GPU_DF_NONE, GPU_AF_NONE);
        setChanAmbColor(GPU_COLOR0A0, black);
        setChanMatColor(GPU_COLOR0A0, white);
        setChanCtrl(GPU_COLOR1A1, GPU_FALSE, GPU_SRC_REG, GPU_SRC_VTX, GPU_LIGHT_NULL, GPU_DF_NONE, GPU_AF_NONE);
        setChanAmbColor(GPU_COLOR1A1, black);
        setChanMatColor(GPU_COLOR1A1, white);

        static GpuTexObj sDefaultTexture;
        J3DNativeTexture::registerImage(&sDefaultTexData, GPU_TF_IA8, 4, 4, 1);
        J3DNativeTexture::init(&sDefaultTexture, &sDefaultTexData, 4, 4, GPU_TF_IA8, GPU_CLAMP, GPU_CLAMP, GPU_FALSE);
        for (u32 i = 0; i < 8; i++) {
            J3DNativeTexture::load(&sDefaultTexture, static_cast< GpuTexMapID >(i));
        }

        for (u32 i = 0; i < 8; i++) {
            setTevOrder(static_cast< GpuTevStageID >(i), static_cast< GpuTexCoordID >(i), static_cast< GpuTexMapID >(i),
                        GPU_COLOR0A0);
        }
        for (u32 i = 8; i < 16; i++) {
            setTevOrder(static_cast< GpuTevStageID >(i), GPU_TEXCOORD_NULL, GPU_TEXMAP_NULL, GPU_COLOR_NULL);
        }
        setNumTevStages(1);
        setTevOp(GPU_TEVSTAGE0, GPU_REPLACE);
        setAlphaCompare(GPU_ALWAYS, 0, GPU_AOP_AND, GPU_ALWAYS, 0);
        setZTexture(GPU_ZT_DISABLE, GPU_TF_Z8, 0);
        for (u32 i = 0; i < 16; i++) {
            setTevKColorSel(static_cast< GpuTevStageID >(i), GPU_TEV_KCSEL_1_4);
            setTevKAlphaSel(static_cast< GpuTevStageID >(i), GPU_TEV_KASEL_1);
            setTevSwapMode(static_cast< GpuTevStageID >(i), GPU_TEV_SWAP0, GPU_TEV_SWAP0);
        }
        setTevSwapModeTable(GPU_TEV_SWAP0, GPU_CH_RED, GPU_CH_GREEN, GPU_CH_BLUE, GPU_CH_ALPHA);
        setTevSwapModeTable(GPU_TEV_SWAP1, GPU_CH_RED, GPU_CH_RED, GPU_CH_RED, GPU_CH_ALPHA);
        setTevSwapModeTable(GPU_TEV_SWAP2, GPU_CH_GREEN, GPU_CH_GREEN, GPU_CH_GREEN, GPU_CH_ALPHA);
        setTevSwapModeTable(GPU_TEV_SWAP3, GPU_CH_BLUE, GPU_CH_BLUE, GPU_CH_BLUE, GPU_CH_ALPHA);
        for (u32 i = 0; i < 16; i++) {
            setTevDirect(static_cast< GpuTevStageID >(i));
        }
        setNumIndStages(0);
        for (u32 i = 0; i < 4; i++) {
            setIndTexCoordScale(static_cast< GpuIndTexStageID >(i), GPU_ITS_1, GPU_ITS_1);
        }

        setFog(GPU_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, black);
        setFogRangeAdj(GPU_FALSE, 0, nullptr);
        setBlendMode(GPU_BM_NONE, GPU_BL_SRCALPHA, GPU_BL_INVSRCALPHA, GPU_LO_CLEAR);
        setColorUpdate(GPU_TRUE);
        setAlphaUpdate(GPU_TRUE);
        setZMode(GPU_TRUE, GPU_LEQUAL, GPU_TRUE);
        setDither(GPU_TRUE);
        setDstAlpha(GPU_FALSE, 0);
        setFrameAlpha(false);
    }

    void showFrame(u16 width, u16 height, bool filtered) {
        J3DNativeShape::flushState();
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_show(pHost->user, width, height, filtered ? NEBULA_CAPTURE_FILTERED : 0u);
    }

    void endFrame() {
        const NebulaHostServices* pHost = nebula_host();
        pHost->frame_end(pHost->user);
    }

    void setZTexture(GpuZTexOp op, GpuTexFmt format, u32 bias) {
        NebulaStateOp storage;
        J3DStateList list(&storage, 1);
        list.setZTexture(op, format, bias);
        list.send();
    }

    void setFogRangeAdj(GpuBool enable, u16 center, const GpuFogAdjTable* pTable) {
        NebulaStateOp ops[6];
        J3DStateList list(ops, 6);
        list.setFogRangeAdj(enable, center, pTable);
        list.send();
    }

    void setTevOrder(GpuTevStageID stage, GpuTexCoordID coord, GpuTexMapID map, GpuChannelID color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevStageOrder(stage, coord, map, color);
        list.send();
    }

    void setIndTexOrder(GpuIndTexStageID stage, GpuTexCoordID coord, GpuTexMapID map) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setIndTexOrder(stage, coord, map);
        list.send();
    }

    void enableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.enableTexOffsets(coord, lines, points);
        list.send();
    }

    void setCullMode(GpuCullMode mode) {
        sCullMode = mode;
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCullMode(mode);
        list.send();
    }

    void getCullMode(GpuCullMode* pMode) {
        *pMode = sCullMode;
    }
};  // namespace J3DNativeState
void J3DNativeStateSetTexCoordGen2(GpuTexCoordID coord, GpuTexGenType type, GpuTexGenSrc src, u32 mtx, GpuBool normalize, u32 postMtx) {
    J3DNativeState::setTexCoordGen2(coord, type, src, mtx, normalize, postMtx);
}

void J3DNativeStateSetNumTexGens(u8 count) {
    J3DNativeState::setNumTexGens(count);
}

void J3DNativeStateSetNumChans(u8 count) {
    J3DNativeState::setNumChans(count);
}

void J3DNativeStateSetNumTevStages(u8 count) {
    J3DNativeState::setNumTevStages(count);
}

void J3DNativeStateSetNumIndStages(u8 count) {
    J3DNativeState::setNumIndStages(count);
}

void J3DNativeStateSetCoPlanar(GpuBool enable) {
    J3DNativeState::setCoPlanar(enable);
}

void J3DNativeStateSetCurrentMtx(u32 id) {
    J3DNativeState::setCurrentMtx(id);
}

void J3DNativeStateSetTevIndirect(GpuTevStageID stage, GpuIndTexStageID indStage, GpuIndTexFormat format, GpuIndTexBiasSel bias, GpuIndTexMtxID matrix, GpuIndTexWrap wrapS, GpuIndTexWrap wrapT, GpuBool addPrev, GpuBool utcLod, GpuIndTexAlphaSel alpha) {
    J3DNativeState::setTevIndirect(stage, indStage, format, bias, matrix, wrapS, wrapT, addPrev, utcLod, alpha);
}

void J3DNativeStateSetTevIndWarp(GpuTevStageID stage, GpuIndTexStageID indStage, GpuBool signedOffset, GpuBool replaceMode, GpuIndTexMtxID matrix) {
    J3DNativeState::setTevIndWarp(stage, indStage, signedOffset, replaceMode, matrix);
}

void J3DNativeStateSetIndTexCoordScale(GpuIndTexStageID indStage, GpuIndTexScale scaleS, GpuIndTexScale scaleT) {
    J3DNativeState::setIndTexCoordScale(indStage, scaleS, scaleT);
}

void J3DNativeStateSetLineWidth(u8 width, GpuTexOffset texOffsets) {
    J3DNativeState::setLineWidth(width, texOffsets);
}

void J3DNativeStateSetPointSize(u8 size, GpuTexOffset texOffsets) {
    J3DNativeState::setPointSize(size, texOffsets);
}

void J3DNativeStateInitLightColor(GpuLightObj* pObj, NebulaColor8 color) {
    J3DNativeState::initLightColor(pObj, color);
}

void J3DNativeStateInitLightPos(GpuLightObj* pObj, f32 x, f32 y, f32 z) {
    J3DNativeState::initLightPos(pObj, x, y, z);
}

void J3DNativeStateLoadLightObjImm(const GpuLightObj* pObj, GpuLightID id) {
    J3DNativeState::loadLightObjImm(pObj, id);
}

void J3DNativeStateSetTevOrder(GpuTevStageID stage, GpuTexCoordID coord, GpuTexMapID map, GpuChannelID color) {
    J3DNativeState::setTevOrder(stage, coord, map, color);
}

void J3DNativeStateSetIndTexOrder(GpuIndTexStageID stage, GpuTexCoordID coord, GpuTexMapID map) {
    J3DNativeState::setIndTexOrder(stage, coord, map);
}

void J3DNativeStateEnableTexOffsets(GpuTexCoordID coord, GpuBool lines, GpuBool points) {
    J3DNativeState::enableTexOffsets(coord, lines, points);
}

void J3DNativeStateSetCullMode(GpuCullMode mode) {
    J3DNativeState::setCullMode(mode);
}

void J3DNativeStateGetCullMode(GpuCullMode* pMode) {
    J3DNativeState::getCullMode(pMode);
}

void J3DNativeStateSetChanMatColor(GpuChannelID chan, NebulaColor8 color) {
    J3DNativeState::setChanMatColor(chan, color);
}

void J3DNativeStateSetChanAmbColor(GpuChannelID chan, NebulaColor8 color) {
    J3DNativeState::setChanAmbColor(chan, color);
}

void J3DNativeStateSetZMode(GpuBool compare, GpuCompare func, GpuBool update) {
    J3DNativeState::setZMode(compare, func, update);
}

void J3DNativeStateSetAlphaCompare(GpuCompare comp0, u8 ref0, GpuAlphaOp op, GpuCompare comp1, u8 ref1) {
    J3DNativeState::setAlphaCompare(comp0, ref0, op, comp1, ref1);
}

void J3DNativeStateSetDstAlpha(GpuBool enable, u8 alpha) {
    J3DNativeState::setDstAlpha(enable, alpha);
}

void J3DNativeStateSetTevColor(GpuTevRegID id, NebulaColor8 color) {
    J3DNativeState::setTevColor(id, color);
}

void J3DNativeStateSetTevKColor(GpuTevKColorID id, NebulaColor8 color) {
    J3DNativeState::setTevKColor(id, color);
}

void J3DNativeStateSetChanCtrl(GpuChannelID chan, GpuBool enable, GpuColorSrc ambSrc, GpuColorSrc matSrc, u32 lightMask, GpuDiffuseFn diffFn, GpuAttnFn attnFn) {
    J3DNativeState::setChanCtrl(chan, enable, ambSrc, matSrc, lightMask, diffFn, attnFn);
}

void J3DNativeStateSetTevDirect(GpuTevStageID stage) {
    J3DNativeState::setTevDirect(stage);
}

void J3DNativeStateSetFog(GpuFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, NebulaColor8 color) {
    J3DNativeState::setFog(type, start, end, nearZ, farZ, color);
}

void J3DNativeStateSetBlendMode(GpuBlendMode type, GpuBlendFactor src, GpuBlendFactor dst, GpuLogicOp op) {
    J3DNativeState::setBlendMode(type, src, dst, op);
}

void J3DNativeStateSetTevColorIn(GpuTevStageID stage, GpuTevColorArg a, GpuTevColorArg b, GpuTevColorArg c, GpuTevColorArg d) {
    J3DNativeState::setTevColorIn(stage, a, b, c, d);
}

void J3DNativeStateSetTevAlphaIn(GpuTevStageID stage, GpuTevAlphaArg a, GpuTevAlphaArg b, GpuTevAlphaArg c, GpuTevAlphaArg d) {
    J3DNativeState::setTevAlphaIn(stage, a, b, c, d);
}

void J3DNativeStateSetTevColorOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out) {
    J3DNativeState::setTevColorOp(stage, op, bias, scale, clamp, out);
}

void J3DNativeStateSetTevAlphaOp(GpuTevStageID stage, GpuTevOp op, GpuTevBias bias, GpuTevScale scale, GpuBool clamp, GpuTevRegID out) {
    J3DNativeState::setTevAlphaOp(stage, op, bias, scale, clamp, out);
}

void J3DNativeStateSetTevKColorSel(GpuTevStageID stage, GpuTevKColorSel sel) {
    J3DNativeState::setTevKColorSel(stage, sel);
}

void J3DNativeStateSetTevKAlphaSel(GpuTevStageID stage, GpuTevKAlphaSel sel) {
    J3DNativeState::setTevKAlphaSel(stage, sel);
}

void J3DNativeStateSetTevSwapMode(GpuTevStageID stage, GpuTevSwapSel rasSwap, GpuTevSwapSel texSwap) {
    J3DNativeState::setTevSwapMode(stage, rasSwap, texSwap);
}

void J3DNativeStateSetTevSwapModeTable(GpuTevSwapSel table, GpuTevColorChan red, GpuTevColorChan green, GpuTevColorChan blue, GpuTevColorChan alpha) {
    J3DNativeState::setTevSwapModeTable(table, red, green, blue, alpha);
}

void J3DNativeStateSetColorUpdate(GpuBool enable) {
    J3DNativeState::setColorUpdate(enable);
}

void J3DNativeStateSetAlphaUpdate(GpuBool enable) {
    J3DNativeState::setAlphaUpdate(enable);
}

void J3DNativeStateSetDither(GpuBool enable) {
    J3DNativeState::setDither(enable);
}
