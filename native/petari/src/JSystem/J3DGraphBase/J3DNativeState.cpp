#include "JSystem/J3DGraphBase/J3DNativeState.hpp"
#include "JSystem/J3DGraphBase/J3DStateList.hpp"
#include <math.h>
#include <string.h>

namespace {
    // GXInit's cull mode and pixel format.
    GXCullMode sCullMode = GX_CULL_BACK;
    GXPixelFmt sPixelFormat = GX_PF_RGB8_Z24;
    GXZFmt16 sZFormat = GX_ZC_LINEAR;

    // The EFB copy settings: GXInit's (its NTSC 480 mode) until the game sets them.
    struct CopySettings {
        u16 mDispSrc[4] = {0, 0, 640, 480};
        u16 mTexSrc[4] = {0, 0, 0, 0};
        u16 mDispWidth = 640;
        u16 mTexSize[2] = {0, 0};
        u8 mTexFormat = GX_TF_I4;
        u8 mTexMipmap = 0;
        u8 mClampTop = 1, mClampBottom = 1;
        u8 mDispInterlaced = GX_COPY_PROGRESSIVE;
        u8 mDispVerticalScale = 0;
        u8 mDispGamma = GX_GM_1_0;
    };
    CopySettings sCopy;

    // GX's count of XFB lines a copy of `efbHeight` lines at scale 256 / `scale` makes.
    u32 numXfbLines(u32 efbHeight, u32 scale) {
        u32 lines = ((efbHeight - 1) * 256) / scale + 1;
        u32 odd = scale;
        if (odd > 0x80 && odd < 0x100) {
            while ((odd & 1) == 0) {
                odd >>= 1;
            }
            if (efbHeight % odd == 0) {
                lines++;
            }
        }
        return lines > 1024 ? 1024 : lines;
    }

    void sendCopy(void* pDest, GXBool clear, bool display) {
        NebulaStateOp op;
        memset(&op, 0, sizeof(op));
        op.a[0] = display;
        op.a[1] = clear;
        op.a[2] = sCopy.mTexFormat;
        op.a[3] = sCopy.mTexMipmap;
        op.a[4] = sCopy.mClampTop;
        op.a[5] = sCopy.mClampBottom;
        op.a[6] = display ? sCopy.mDispInterlaced : GX_COPY_PROGRESSIVE;
        op.a[7] = sCopy.mDispVerticalScale;
        op.a[8] = sCopy.mDispGamma;
        const u16* pSrc = display ? sCopy.mDispSrc : sCopy.mTexSrc;
        for (u32 i = 0; i < 4; i++) {
            op.f[i] = pSrc[i];
        }
        op.size[0] = display ? sCopy.mDispWidth : sCopy.mTexSize[0];
        op.size[1] = display ? 0 : sCopy.mTexSize[1];
        op.image = reinterpret_cast< uintptr_t >(pDest);
        NebulaStateOp storage;
        J3DStateList list(&storage, 1);
        list.copy(op);
        list.send();
    }

    static_assert(sizeof(J3DNativeLight) <= sizeof(GXLightObj), "a GXLightObj holds a J3DNativeLight");

    J3DNativeLight& light(GXLightObj* pObj) {
        return *reinterpret_cast< J3DNativeLight* >(pObj);
    }
}  // namespace

namespace J3DNativeState {
    void setZMode(GXBool compare, GXCompare func, GXBool update) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setZMode(compare, func, update);
        list.send();
    }

    void setAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp alphaOp, GXCompare comp1, u8 ref1) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setAlphaCompare(comp0, ref0, alphaOp, comp1, ref1);
        list.send();
    }

    void setDstAlpha(GXBool enable, u8 alpha) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setDstAlpha(enable, alpha);
        list.send();
    }

    void setTevColor(GXTevRegID id, GXColor color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevColor(id, color);
        list.send();
    }

    void setTevKColor(GXTevKColorID id, GXColor color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevKColor(id, color);
        list.send();
    }

    void setTevColorS10(GXTevRegID id, GXColorS10 color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevColorS10(id, color);
        list.send();
    }

    void setChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask,
                     GXDiffuseFn diffFn, GXAttnFn attnFn) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setChanCtrl(chan, enable, ambSrc, matSrc, lightMask, diffFn, attnFn);
        list.send();
    }

    void setTevDirect(GXTevStageID stage) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevDirect(stage);
        list.send();
    }

    void setFog(GXFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, GXColor color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setFog(type, start, end, nearZ, farZ, color);
        op.a[5] = 1;
        list.send();
    }

    void setClipMode(GXClipMode mode) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setClipMode(mode);
        list.send();
    }

    void setIndTexMtx(GXIndTexMtxID id, const f32 offset[2][3], s8 scaleExp) {
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

    void setBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setBlendMode(type, src, dst, op);
        list.send();
    }

    void setTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevColorIn(stage, a, b, c, d);
        list.send();
    }

    void setTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevAlphaIn(stage, a, b, c, d);
        list.send();
    }

    void setTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevColorOp(stage, op, bias, scale, clamp, out);
        list.send();
    }

    void setTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevAlphaOp(stage, op, bias, scale, clamp, out);
        list.send();
    }

    void setTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevKColorSel(stage, sel);
        list.send();
    }

    void setTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevKAlphaSel(stage, sel);
        list.send();
    }

    void setTevSwapMode(GXTevStageID stage, GXTevSwapSel rasSwap, GXTevSwapSel texSwap) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevSwapMode(stage, rasSwap, texSwap);
        list.send();
    }

    void setTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setTevSwapModeTable(table, red, green, blue, alpha);
        list.send();
    }

    void setColorUpdate(GXBool enable) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setColorUpdate(enable);
        list.send();
    }

    void setAlphaUpdate(GXBool enable) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setAlphaUpdate(enable);
        list.send();
    }

    void setDither(GXBool enable) {
        NebulaStateOp ops[1];
        J3DStateList list(ops, 1);
        list.setDither(enable);
        list.send();
    }

    void setTevOp(GXTevStageID stage, GXTevMode mode) {
        NebulaStateOp ops[4];
        J3DStateList list(ops, 4);
        list.setTevOp(stage, mode);
        list.send();
    }

    void setChanMatColor(GXChannelID chan, GXColor color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setChanMatColor(chan, color);
        op.a[5] = 1;
        list.send();
    }

    void setChanAmbColor(GXChannelID chan, GXColor color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setChanAmbColor(chan, color);
        op.a[5] = 1;
        list.send();
    }

    void setTexCoordGen2(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize, u32 postMtx) {
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

    void setCoPlanar(GXBool enable) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCoPlanar(enable);
        list.send();
    }

    void setZCompLoc(GXBool beforeTex) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setZCompLoc(beforeTex);
        list.send();
    }

    void setCurrentMtx(u32 id) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCurrentMtx(id);
        list.send();
    }

    void setTevIndirect(GXTevStageID stage, GXIndTexStageID indStage, GXIndTexFormat format, GXIndTexBiasSel bias, GXIndTexMtxID matrix, GXIndTexWrap wrapS, GXIndTexWrap wrapT, GXBool addPrev, GXBool utcLod, GXIndTexAlphaSel alpha) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevIndirect(stage, indStage, format, bias, matrix, wrapS, wrapT, addPrev, utcLod, alpha);
        list.send();
    }

    void setTevIndWarp(GXTevStageID stage, GXIndTexStageID indStage, GXBool signedOffset, GXBool replaceMode, GXIndTexMtxID matrix) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevIndWarp(stage, indStage, signedOffset, replaceMode, matrix);
        list.send();
    }

    void setIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setIndTexCoordScale(indStage, scaleS, scaleT);
        list.send();
    }

    void setLineWidth(u8 width, GXTexOffset texOffsets) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setLineWidth(width, texOffsets);
        list.send();
    }

    void setPointSize(u8 size, GXTexOffset texOffsets) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setPointSize(size, texOffsets);
        list.send();
    }

    void initLightColor(GXLightObj* pObj, GXColor color) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mColor[0] = color.r;
        rLight.mColor[1] = color.g;
        rLight.mColor[2] = color.b;
        rLight.mColor[3] = color.a;
    }

    void initLightPos(GXLightObj* pObj, f32 x, f32 y, f32 z) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mPosition[0] = x;
        rLight.mPosition[1] = y;
        rLight.mPosition[2] = z;
    }

    void initLightDir(GXLightObj* pObj, f32 nx, f32 ny, f32 nz) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mDirection[0] = -nx;
        rLight.mDirection[1] = -ny;
        rLight.mDirection[2] = -nz;
    }

    // GX's distance for a light at infinity.
    const f32 cInfiniteLightDistance = 1.0e18f;

    void initSpecularDir(GXLightObj* pObj, f32 nx, f32 ny, f32 nz) {
        const f32 hx = -nx;
        const f32 hy = -ny;
        const f32 hz = 1.0f - nz;
        f32 mag = hx * hx + hy * hy + hz * hz;
        if (mag != 0.0f) {
            mag = 1.0f / sqrtf(mag);
        }
        initSpecularDirHA(pObj, nx, ny, nz, hx * mag, hy * mag, hz * mag);
    }

    void initSpecularDirHA(GXLightObj* pObj, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mDirection[0] = hx;
        rLight.mDirection[1] = hy;
        rLight.mDirection[2] = hz;
        rLight.mPosition[0] = nx * -cInfiniteLightDistance;
        rLight.mPosition[1] = ny * -cInfiniteLightDistance;
        rLight.mPosition[2] = nz * -cInfiniteLightDistance;
    }

    void initLightAttn(GXLightObj* pObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
        J3DNativeLight& rLight = light(pObj);
        rLight.mAngle[0] = a0;
        rLight.mAngle[1] = a1;
        rLight.mAngle[2] = a2;
        rLight.mDistance[0] = k0;
        rLight.mDistance[1] = k1;
        rLight.mDistance[2] = k2;
    }

    void initLightSpot(GXLightObj* pObj, f32 cutoff, GXSpotFn spotFn) {
        if (cutoff <= 0.0f || cutoff > 90.0f) {
            spotFn = GX_SP_OFF;
        }
        const f32 cr = cos(cutoff * 3.14159265358979323846f / 180.0f);
        f32 a0, a1, a2, d;
        switch (spotFn) {
        case GX_SP_FLAT:
            a0 = -1000.0f * cr;
            a1 = 1000.0f;
            a2 = 0.0f;
            break;
        case GX_SP_COS:
            a1 = 1.0f / (1.0f - cr);
            a0 = -cr * a1;
            a2 = 0.0f;
            break;
        case GX_SP_COS2:
            a2 = 1.0f / (1.0f - cr);
            a0 = 0.0f;
            a1 = -cr * a2;
            break;
        case GX_SP_SHARP:
            d = 1.0f / ((1.0f - cr) * (1.0f - cr));
            a0 = cr * (cr - 2.0f) * d;
            a1 = 2.0f * d;
            a2 = -d;
            break;
        case GX_SP_RING1:
            d = 1.0f / ((1.0f - cr) * (1.0f - cr));
            a2 = -4.0f * d;
            a0 = a2 * cr;
            a1 = 4.0f * (1.0f + cr) * d;
            break;
        case GX_SP_RING2:
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

    void initLightDistAttn(GXLightObj* pObj, f32 refDistance, f32 refBrightness, GXDistAttnFn distFn) {
        if (refDistance < 0.0f || refBrightness <= 0.0f || refBrightness >= 1.0f) {
            distFn = GX_DA_OFF;
        }
        f32 k0 = 1.0f, k1 = 0.0f, k2 = 0.0f;
        switch (distFn) {
        case GX_DA_GENTLE:
            k1 = (1.0f - refBrightness) / (refBrightness * refDistance);
            break;
        case GX_DA_MEDIUM:
            k1 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance);
            k2 = 0.5f * (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
            break;
        case GX_DA_STEEP:
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

    void loadLightObjImm(const GXLightObj* pObj, GXLightID id) {
        NebulaStateOp ops[2];
        J3DStateList list(ops, 2);
        list.setLight(id, *reinterpret_cast< const J3DNativeLight* >(pObj));
        list.send();
    }

    void setPixelFmt(GXPixelFmt format, GXZFmt16 zFormat) {
        // GXSetPixelFmt compares the hardware's type, which three YUV formats share.
        static const u8 sHardwareType[8] = {0, 1, 2, 3, 4, 4, 4, 5};
        const bool changed = sHardwareType[format & 7] != sHardwareType[sPixelFormat & 7] || zFormat != sZFormat;
        sPixelFormat = format;
        sZFormat = zFormat;
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setPixelFmt(format, zFormat, changed);
        list.send();
    }

    void invalidateTexAll() {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.invalidateTexAll();
        list.send();
    }

    void setDispCopySrc(u16 left, u16 top, u16 width, u16 height) {
        sCopy.mDispSrc[0] = left;
        sCopy.mDispSrc[1] = top;
        sCopy.mDispSrc[2] = width;
        sCopy.mDispSrc[3] = height;
    }

    void setTexCopySrc(u16 left, u16 top, u16 width, u16 height) {
        sCopy.mTexSrc[0] = left;
        sCopy.mTexSrc[1] = top;
        sCopy.mTexSrc[2] = width;
        sCopy.mTexSrc[3] = height;
    }

    void setDispCopyDst(u16 width, u16 height) {
        sCopy.mDispWidth = width;
    }

    void setTexCopyDst(u16 width, u16 height, GXTexFmt format, GXBool mipmap) {
        sCopy.mTexSize[0] = width;
        sCopy.mTexSize[1] = height;
        sCopy.mTexFormat = static_cast< u8 >(format);
        sCopy.mTexMipmap = mipmap;
    }

    void setDispCopyFrame2Field(GXCopyMode mode) {
        sCopy.mDispInterlaced = static_cast< u8 >(mode);
    }

    void setCopyClamp(GXFBClamp clamp) {
        sCopy.mClampTop = (clamp & GX_CLAMP_TOP) == GX_CLAMP_TOP;
        sCopy.mClampBottom = (clamp & GX_CLAMP_BOTTOM) == GX_CLAMP_BOTTOM;
    }

    void setDispCopyGamma(GXGamma gamma) {
        sCopy.mDispGamma = static_cast< u8 >(gamma);
    }

    u32 setDispCopyYScale(f32 scale) {
        const u32 iScale = static_cast< u32 >(256.0f / scale) & 0x1FF;
        sCopy.mDispVerticalScale = iScale != 0x100;
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCopyYScale(iScale);
        list.send();
        return numXfbLines(sCopy.mDispSrc[3], iScale);
    }

    u16 getNumXfbLines(u16 efbHeight, f32 scale) {
        return static_cast< u16 >(numXfbLines(efbHeight, static_cast< u32 >(256.0f / scale) & 0x1FF));
    }

    f32 getYScaleFactor(u16 efbHeight, u16 xfbHeight) {
        u32 target = xfbHeight;
        f32 yScale = static_cast< f32 >(xfbHeight) / static_cast< f32 >(efbHeight);
        u32 lines = numXfbLines(efbHeight, static_cast< u32 >(256.0f / yScale) & 0x1FF);
        while (lines > xfbHeight) {
            target--;
            yScale = static_cast< f32 >(target) / static_cast< f32 >(efbHeight);
            lines = numXfbLines(efbHeight, static_cast< u32 >(256.0f / yScale) & 0x1FF);
        }
        f32 fScale = yScale;
        while (lines < xfbHeight) {
            fScale = yScale;
            target++;
            yScale = static_cast< f32 >(target) / static_cast< f32 >(efbHeight);
            lines = numXfbLines(efbHeight, static_cast< u32 >(256.0f / yScale) & 0x1FF);
        }
        return fScale;
    }

    void setCopyClear(GXColor color, u32 depth) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCopyClear(color, depth);
        list.send();
    }

    void setCopyFilter(GXBool antialias, const u8 samples[12][2], GXBool verticalFilter, const u8 coefficients[7]) {
        NebulaStateOp ops[2];
        J3DStateList list(ops, 2);
        list.setCopyFilter(antialias, samples, verticalFilter, coefficients);
        list.send();
    }

    void copyDisp(void* pDest, GXBool clear) {
        sendCopy(pDest, clear, true);
    }

    void copyTex(void* pDest, GXBool clear) {
        sendCopy(pDest, clear, false);
    }

    void setZTexture(GXZTexOp op, GXTexFmt format, u32 bias) {
        NebulaStateOp storage;
        J3DStateList list(&storage, 1);
        list.setZTexture(op, format, bias);
        list.send();
    }

    void setFogRangeAdj(GXBool enable, u16 center, const GXFogAdjTable* pTable) {
        NebulaStateOp ops[6];
        J3DStateList list(ops, 6);
        list.setFogRangeAdj(enable, center, pTable);
        list.send();
    }

    void setTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setTevStageOrder(stage, coord, map, color);
        list.send();
    }

    void setIndTexOrder(GXIndTexStageID stage, GXTexCoordID coord, GXTexMapID map) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setIndTexOrder(stage, coord, map);
        list.send();
    }

    void enableTexOffsets(GXTexCoordID coord, GXBool lines, GXBool points) {
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.enableTexOffsets(coord, lines, points);
        list.send();
    }

    void setCullMode(GXCullMode mode) {
        sCullMode = mode;
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.setCullMode(mode);
        list.send();
    }

    void getCullMode(GXCullMode* pMode) {
        *pMode = sCullMode;
    }
};  // namespace J3DNativeState
void J3DNativeStateSetTexCoordGen2(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src, u32 mtx, GXBool normalize, u32 postMtx) {
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

void J3DNativeStateSetCoPlanar(GXBool enable) {
    J3DNativeState::setCoPlanar(enable);
}

void J3DNativeStateSetZCompLoc(GXBool beforeTex) {
    J3DNativeState::setZCompLoc(beforeTex);
}

void J3DNativeStateSetCurrentMtx(u32 id) {
    J3DNativeState::setCurrentMtx(id);
}

void J3DNativeStateSetTevIndirect(GXTevStageID stage, GXIndTexStageID indStage, GXIndTexFormat format, GXIndTexBiasSel bias, GXIndTexMtxID matrix, GXIndTexWrap wrapS, GXIndTexWrap wrapT, GXBool addPrev, GXBool utcLod, GXIndTexAlphaSel alpha) {
    J3DNativeState::setTevIndirect(stage, indStage, format, bias, matrix, wrapS, wrapT, addPrev, utcLod, alpha);
}

void J3DNativeStateSetTevIndWarp(GXTevStageID stage, GXIndTexStageID indStage, GXBool signedOffset, GXBool replaceMode, GXIndTexMtxID matrix) {
    J3DNativeState::setTevIndWarp(stage, indStage, signedOffset, replaceMode, matrix);
}

void J3DNativeStateSetIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT) {
    J3DNativeState::setIndTexCoordScale(indStage, scaleS, scaleT);
}

void J3DNativeStateSetLineWidth(u8 width, GXTexOffset texOffsets) {
    J3DNativeState::setLineWidth(width, texOffsets);
}

void J3DNativeStateSetPointSize(u8 size, GXTexOffset texOffsets) {
    J3DNativeState::setPointSize(size, texOffsets);
}

void J3DNativeStateInitLightColor(GXLightObj* pObj, GXColor color) {
    J3DNativeState::initLightColor(pObj, color);
}

void J3DNativeStateInitLightPos(GXLightObj* pObj, f32 x, f32 y, f32 z) {
    J3DNativeState::initLightPos(pObj, x, y, z);
}

void J3DNativeStateLoadLightObjImm(const GXLightObj* pObj, GXLightID id) {
    J3DNativeState::loadLightObjImm(pObj, id);
}

void J3DNativeStateSetPixelFmt(GXPixelFmt format, GXZFmt16 zFormat) {
    J3DNativeState::setPixelFmt(format, zFormat);
}

void J3DNativeStateInvalidateTexAll(void) {
    J3DNativeState::invalidateTexAll();
}

void J3DNativeStateSetTexCopySrc(u16 left, u16 top, u16 width, u16 height) {
    J3DNativeState::setTexCopySrc(left, top, width, height);
}

void J3DNativeStateSetTexCopyDst(u16 width, u16 height, GXTexFmt format, GXBool mipmap) {
    J3DNativeState::setTexCopyDst(width, height, format, mipmap);
}

void J3DNativeStateSetCopyClamp(GXFBClamp clamp) {
    J3DNativeState::setCopyClamp(clamp);
}

void J3DNativeStateSetCopyClear(GXColor color, u32 depth) {
    J3DNativeState::setCopyClear(color, depth);
}

void J3DNativeStateSetCopyFilter(GXBool antialias, const u8 samples[12][2], GXBool verticalFilter, const u8 coefficients[7]) {
    J3DNativeState::setCopyFilter(antialias, samples, verticalFilter, coefficients);
}

void J3DNativeStateCopyTex(void* pDest, GXBool clear) {
    J3DNativeState::copyTex(pDest, clear);
}

void J3DNativeStateSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color) {
    J3DNativeState::setTevOrder(stage, coord, map, color);
}

void J3DNativeStateSetIndTexOrder(GXIndTexStageID stage, GXTexCoordID coord, GXTexMapID map) {
    J3DNativeState::setIndTexOrder(stage, coord, map);
}

void J3DNativeStateEnableTexOffsets(GXTexCoordID coord, GXBool lines, GXBool points) {
    J3DNativeState::enableTexOffsets(coord, lines, points);
}

void J3DNativeStateSetCullMode(GXCullMode mode) {
    J3DNativeState::setCullMode(mode);
}

void J3DNativeStateGetCullMode(GXCullMode* pMode) {
    J3DNativeState::getCullMode(pMode);
}

void J3DNativeStateSetChanMatColor(GXChannelID chan, GXColor color) {
    J3DNativeState::setChanMatColor(chan, color);
}

void J3DNativeStateSetChanAmbColor(GXChannelID chan, GXColor color) {
    J3DNativeState::setChanAmbColor(chan, color);
}

void J3DNativeStateSetZMode(GXBool compare, GXCompare func, GXBool update) {
    J3DNativeState::setZMode(compare, func, update);
}

void J3DNativeStateSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
    J3DNativeState::setAlphaCompare(comp0, ref0, op, comp1, ref1);
}

void J3DNativeStateSetDstAlpha(GXBool enable, u8 alpha) {
    J3DNativeState::setDstAlpha(enable, alpha);
}

void J3DNativeStateSetTevColor(GXTevRegID id, GXColor color) {
    J3DNativeState::setTevColor(id, color);
}

void J3DNativeStateSetTevKColor(GXTevKColorID id, GXColor color) {
    J3DNativeState::setTevKColor(id, color);
}

void J3DNativeStateSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask, GXDiffuseFn diffFn, GXAttnFn attnFn) {
    J3DNativeState::setChanCtrl(chan, enable, ambSrc, matSrc, lightMask, diffFn, attnFn);
}

void J3DNativeStateSetTevDirect(GXTevStageID stage) {
    J3DNativeState::setTevDirect(stage);
}

void J3DNativeStateSetFog(GXFogType type, f32 start, f32 end, f32 nearZ, f32 farZ, GXColor color) {
    J3DNativeState::setFog(type, start, end, nearZ, farZ, color);
}

void J3DNativeStateSetBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op) {
    J3DNativeState::setBlendMode(type, src, dst, op);
}

void J3DNativeStateSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {
    J3DNativeState::setTevColorIn(stage, a, b, c, d);
}

void J3DNativeStateSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
    J3DNativeState::setTevAlphaIn(stage, a, b, c, d);
}

void J3DNativeStateSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out) {
    J3DNativeState::setTevColorOp(stage, op, bias, scale, clamp, out);
}

void J3DNativeStateSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out) {
    J3DNativeState::setTevAlphaOp(stage, op, bias, scale, clamp, out);
}

void J3DNativeStateSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) {
    J3DNativeState::setTevKColorSel(stage, sel);
}

void J3DNativeStateSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) {
    J3DNativeState::setTevKAlphaSel(stage, sel);
}

void J3DNativeStateSetTevSwapMode(GXTevStageID stage, GXTevSwapSel rasSwap, GXTevSwapSel texSwap) {
    J3DNativeState::setTevSwapMode(stage, rasSwap, texSwap);
}

void J3DNativeStateSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {
    J3DNativeState::setTevSwapModeTable(table, red, green, blue, alpha);
}

void J3DNativeStateSetColorUpdate(GXBool enable) {
    J3DNativeState::setColorUpdate(enable);
}

void J3DNativeStateSetAlphaUpdate(GXBool enable) {
    J3DNativeState::setAlphaUpdate(enable);
}

void J3DNativeStateSetDither(GXBool enable) {
    J3DNativeState::setDither(enable);
}
