#include "JSystem/J3DGraphBase/J3DNativeTexture.hpp"
#include "JSystem/J3DGraphBase/J3DStateList.hpp"
#include <nebula_gx_fifo.h>
#include <string.h>

static_assert(sizeof(J3DNativeTexObj) <= sizeof(GXTexObj), "a GXTexObj holds a J3DNativeTexObj");
static_assert(sizeof(J3DNativeTlutObj) <= sizeof(GXTlutObj), "a GXTlutObj holds a J3DNativeTlutObj");

namespace {
    J3DNativeTexObj& texture(GXTexObj* pObj) {
        return *reinterpret_cast< J3DNativeTexObj* >(pObj);
    }

    const J3DNativeTexObj& texture(const GXTexObj* pObj) {
        return *reinterpret_cast< const J3DNativeTexObj* >(pObj);
    }

    // The format of the palette last loaded into each of GX's 20 palette places.
    u8 sTlutFormat[20];

    bool isColorIndexed(GXTexFmt format) {
        return format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2;
    }
}  // namespace

extern "C" const NebulaHostServices* nebula_host(void);

namespace J3DNativeTexture {
    u32 imageSize(u8 format, u16 width, u16 height, u8 levels) {
        // A tile is 32 bytes (RGBA8 and Z24X8: two of them) of 8x8, 8x4 or 4x4 texels by the format's bits per texel
        // (texture and copy formats); an unknown format is a tile per texel, as GX counts it.
        u32 shiftX, shiftY;
        switch (format) {
        case GX_TF_C4:
        case GX_TF_I4:
        case GX_TF_CMPR:
        case GX_CTF_R4:
        case GX_CTF_Z4:
            shiftX = 3;
            shiftY = 3;
            break;
        case GX_TF_C8:
        case GX_TF_I8:
        case GX_TF_IA4:
        case GX_TF_A8:
        case GX_TF_Z8:
        case GX_CTF_RA4:
        case GX_CTF_R8:
        case GX_CTF_G8:
        case GX_CTF_B8:
        case GX_CTF_Z8M:
        case GX_CTF_Z8L:
            shiftX = 3;
            shiftY = 2;
            break;
        case GX_TF_C14X2:
        case GX_TF_IA8:
        case GX_TF_RGB565:
        case GX_TF_RGB5A3:
        case GX_TF_RGBA8:
        case GX_TF_Z16:
        case GX_TF_Z24X8:
        case GX_CTF_RA8:
        case GX_CTF_RG8:
        case GX_CTF_GB8:
        case GX_CTF_Z16L:
            shiftX = 2;
            shiftY = 2;
            break;
        default:
            shiftX = 0;
            shiftY = 0;
            break;
        }
        const u32 tileBytes = format == GX_TF_RGBA8 || format == GX_TF_Z24X8 ? 64 : 32;
        u32 size = 0;
        u32 w = width, h = height;
        for (u32 level = 0; level < (levels != 0 ? levels : 1); level++) {
            size += ((w + (1 << shiftX) - 1) >> shiftX) * ((h + (1 << shiftY) - 1) >> shiftY) * tileBytes;
            if (w == 1 && h == 1) {
                break;
            }
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
        }
        return size;
    }

    void registerImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels) {
        if (pImage == nullptr || width == 0 || height == 0) {
            return;
        }
        NebulaTextureImage image;
        image.bytes = pImage;
        image.size = imageSize(format, width, height, levels);
        image.width = width;
        image.height = height;
        image.format = format;
        image.levels = levels;
        const NebulaHostServices* pHost = nebula_host();
        pHost->texture_image(pHost->user, __NebulaGXWriter.ptr, &image);
    }

    void registerPalette(const void* pPalette, u16 entries) {
        if (pPalette == nullptr || entries == 0) {
            return;
        }
        NebulaTextureImage palette;
        palette.bytes = pPalette;
        palette.size = static_cast< u32 >(entries) * 2;
        palette.width = entries;
        palette.height = 1;
        palette.format = 0xFF;  // not an image
        palette.levels = 1;
        const NebulaHostServices* pHost = nebula_host();
        pHost->texture_image(pHost->user, __NebulaGXWriter.ptr, &palette);
    }

    void init(GXTexObj* pObj, void* pImage, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrapS,
              GXTexWrapMode wrapT, GXBool mipmap) {
        J3DNativeTexObj& rTex = texture(pObj);
        memset(&rTex, 0, sizeof(rTex));
        rTex.mImage = pImage;
        rTex.mWidth = width;
        rTex.mHeight = height;
        rTex.mFormat = static_cast< u8 >(format);
        rTex.mWrapS = static_cast< u8 >(wrapS);
        rTex.mWrapT = static_cast< u8 >(wrapT);
        rTex.mMagFilter = GX_LINEAR;
        rTex.mMipmap = mipmap;
        rTex.mEdgeLod = GX_TRUE;
        if (mipmap) {
            // A mipmap's levels down to 1x1: the larger side's power of two.
            u32 levels = 0;
            for (u32 side = width > height ? width : height; side > 1; side >>= 1) {
                levels++;
            }
            rTex.mMinFilter = isColorIndexed(format) ? GX_LIN_MIP_NEAR : GX_LIN_MIP_LIN;
            rTex.mMaxLod = static_cast< f32 >(levels);
        } else {
            rTex.mMinFilter = GX_LINEAR;
        }
    }

    void initCI(GXTexObj* pObj, void* pImage, u16 width, u16 height, GXCITexFmt format, GXTexWrapMode wrapS,
                GXTexWrapMode wrapT, GXBool mipmap, u32 tlutName) {
        init(pObj, pImage, width, height, static_cast< GXTexFmt >(format), wrapS, wrapT, mipmap);
        texture(pObj).mPalette = 1;
        texture(pObj).mTlutName = tlutName;
    }

    void initLOD(GXTexObj* pObj, GXTexFilter minFilter, GXTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias,
                 GXBool biasClamp, GXBool edgeLod, GXAnisotropy maxAniso) {
        // GXInitTexObjLOD's clamps and steps: the bias in 1/32 (-4 to 3.99), the LOD range in 1/16 (0 to 10).
        if (lodBias < -4.0f) {
            lodBias = -4.0f;
        } else if (lodBias >= 4.0f) {
            lodBias = 3.99f;
        }
        minLod = minLod < 0.0f ? 0.0f : minLod > 10.0f ? 10.0f : minLod;
        maxLod = maxLod < 0.0f ? 0.0f : maxLod > 10.0f ? 10.0f : maxLod;
        J3DNativeTexObj& rTex = texture(pObj);
        rTex.mMinFilter = static_cast< u8 >(minFilter);
        rTex.mMagFilter = magFilter == GX_LINEAR ? GX_LINEAR : GX_NEAR;
        rTex.mLodBias = static_cast< s8 >(static_cast< s32 >(lodBias * 32.0f)) / 32.0f;
        rTex.mMinLod = static_cast< u8 >(static_cast< s32 >(minLod * 16.0f)) / 16.0f;
        rTex.mMaxLod = static_cast< u8 >(static_cast< s32 >(maxLod * 16.0f)) / 16.0f;
        rTex.mBiasClamp = biasClamp;
        rTex.mEdgeLod = edgeLod;
        rTex.mMaxAniso = static_cast< u8 >(maxAniso);
    }

    void load(const GXTexObj* pObj, GXTexMapID map) {
        const J3DNativeTexObj& rTex = texture(pObj);
        NebulaStateOp ops[4];
        J3DStateList list(ops, 4);
        list.setTexLookupMode(map, static_cast< GXTexWrapMode >(rTex.mWrapS), static_cast< GXTexWrapMode >(rTex.mWrapT),
                              static_cast< GXTexFilter >(rTex.mMinFilter), static_cast< GXTexFilter >(rTex.mMagFilter),
                              rTex.mMinLod, rTex.mMaxLod, rTex.mLodBias, rTex.mBiasClamp, rTex.mEdgeLod,
                              static_cast< GXAnisotropy >(rTex.mMaxAniso));
        // The image's format as the texture unit takes it (a depth texture's low four bits).
        list.setTexImgAttr(map, rTex.mWidth, rTex.mHeight, static_cast< GXTexFmt >(rTex.mFormat & 0xF));
        list.setTexImgPtr(map, rTex.mImage);
        ops[2].a[1] = 1;  // GXLoadTexObj's: the coordinate scales follow the texture
        if (rTex.mPalette) {
            list.setTexTlut(map, static_cast< GXTlutFmt >(sTlutFormat[rTex.mTlutName % 20]), rTex.mTlutName);
        }
        list.send();
    }

    void getAll(const GXTexObj* pObj, void** ppImage, u16* pWidth, u16* pHeight, GXTexFmt* pFormat,
                GXTexWrapMode* pWrapS, GXTexWrapMode* pWrapT, GXBool* pMipmap) {
        const J3DNativeTexObj& rTex = texture(pObj);
        *ppImage = const_cast< void* >(rTex.mImage);
        *pWidth = rTex.mWidth;
        *pHeight = rTex.mHeight;
        *pFormat = static_cast< GXTexFmt >(rTex.mFormat);
        *pWrapS = static_cast< GXTexWrapMode >(rTex.mWrapS);
        *pWrapT = static_cast< GXTexWrapMode >(rTex.mWrapT);
        *pMipmap = rTex.mMipmap;
    }

    void getLODAll(const GXTexObj* pObj, GXTexFilter* pMinFilter, GXTexFilter* pMagFilter, f32* pMinLod, f32* pMaxLod,
                   f32* pLodBias, GXBool* pBiasClamp, GXBool* pEdgeLod, GXAnisotropy* pMaxAniso) {
        const J3DNativeTexObj& rTex = texture(pObj);
        *pMinFilter = static_cast< GXTexFilter >(rTex.mMinFilter);
        *pMagFilter = static_cast< GXTexFilter >(rTex.mMagFilter);
        *pMinLod = rTex.mMinLod;
        *pMaxLod = rTex.mMaxLod;
        *pLodBias = rTex.mLodBias;
        *pBiasClamp = rTex.mBiasClamp;
        *pEdgeLod = rTex.mEdgeLod;
        *pMaxAniso = static_cast< GXAnisotropy >(rTex.mMaxAniso);
    }

    u32 getTlut(const GXTexObj* pObj) {
        return texture(pObj).mTlutName;
    }

    void setTlut(GXTexObj* pObj, u32 tlutName) {
        texture(pObj).mTlutName = tlutName;
    }

    void initTlut(GXTlutObj* pObj, void* pLut, GXTlutFmt format, u16 entries) {
        J3DNativeTlutObj& rTlut = *reinterpret_cast< J3DNativeTlutObj* >(pObj);
        memcpy(rTlut.mLut, &pLut, sizeof(pLut));
        registerPalette(pLut, entries);
        rTlut.mFormat = static_cast< u8 >(format);
        rTlut.mEntries = entries;
    }

    void loadTlut(const GXTlutObj* pObj, u32 tlutName) {
        const J3DNativeTlutObj& rTlut = *reinterpret_cast< const J3DNativeTlutObj* >(pObj);
        sTlutFormat[tlutName % 20] = rTlut.mFormat;
        const void* pLut;
        memcpy(&pLut, rTlut.mLut, sizeof(pLut));
        NebulaStateOp op;
        J3DStateList list(&op, 1);
        list.loadTlut(tlutName, pLut);
        list.send();
    }
};  // namespace J3DNativeTexture

void J3DNativeTextureInit(GXTexObj* pObj, void* pImage, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrapS,
                          GXTexWrapMode wrapT, GXBool mipmap) {
    J3DNativeTexture::init(pObj, pImage, width, height, format, wrapS, wrapT, mipmap);
}

void J3DNativeTextureInitLOD(GXTexObj* pObj, GXTexFilter minFilter, GXTexFilter magFilter, f32 minLod, f32 maxLod,
                             f32 lodBias, GXBool biasClamp, GXBool edgeLod, GXAnisotropy maxAniso) {
    J3DNativeTexture::initLOD(pObj, minFilter, magFilter, minLod, maxLod, lodBias, biasClamp, edgeLod, maxAniso);
}

void J3DNativeTextureLoad(const GXTexObj* pObj, GXTexMapID map) {
    J3DNativeTexture::load(pObj, map);
}

void J3DNativeTextureRegisterImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels) {
    J3DNativeTexture::registerImage(pImage, format, width, height, levels);
}
