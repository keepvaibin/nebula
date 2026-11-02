#include "JSystem/J3DGraphBase/J3DNativeTexture.hpp"
#include "JSystem/J3DGraphBase/J3DStateList.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

static_assert(sizeof(J3DNativeTexObj) <= sizeof(GpuTexObj), "a GpuTexObj holds a J3DNativeTexObj");
static_assert(sizeof(J3DNativeTlutObj) <= sizeof(GpuTlutObj), "a GpuTlutObj holds a J3DNativeTlutObj");

namespace {
    J3DNativeTexObj& texture(GpuTexObj* pObj) {
        return *reinterpret_cast< J3DNativeTexObj* >(pObj);
    }

    const J3DNativeTexObj& texture(const GpuTexObj* pObj) {
        return *reinterpret_cast< const J3DNativeTexObj* >(pObj);
    }

    // The format of the palette last loaded into each of GX's 20 palette places.

    bool isColorIndexed(GpuTexFmt format) {
        return format == GPU_TF_C4 || format == GPU_TF_C8 || format == GPU_TF_C14X2;
    }
}  // namespace

extern "C" const NebulaHostServices* nebula_host(void);

namespace J3DNativeTexture {
    u32 imageSize(u8 format, u16 width, u16 height, u8 levels) {
        // Linear texels the game wrote (nebula_cooked_texture.h): rows of the layout's texels.
        if (format >= NEBULA_TEXTURE_LINEAR_L8 && format <= NEBULA_TEXTURE_LINEAR_RGBA8) {
            return nebula_texture_level_bytes(width, height, levels != 0 ? levels : 1,
                                              nebula_texture_linear_layout(format));
        }
        // A tile is 32 bytes (RGBA8 and Z24X8: two of them) of 8x8, 8x4 or 4x4 texels by the format's bits per texel
        // (texture and copy formats); an unknown format is a tile per texel, as GX counts it.
        u32 shiftX, shiftY;
        switch (format) {
        case GPU_TF_C4:
        case GPU_TF_I4:
        case GPU_TF_CMPR:
        case GPU_CTF_R4:
        case GPU_CTF_Z4:
            shiftX = 3;
            shiftY = 3;
            break;
        case GPU_TF_C8:
        case GPU_TF_I8:
        case GPU_TF_IA4:
        case GPU_TF_A8:
        case GPU_TF_Z8:
        case GPU_CTF_RA4:
        case GPU_CTF_R8:
        case GPU_CTF_G8:
        case GPU_CTF_B8:
        case GPU_CTF_Z8M:
        case GPU_CTF_Z8L:
            shiftX = 3;
            shiftY = 2;
            break;
        case GPU_TF_C14X2:
        case GPU_TF_IA8:
        case GPU_TF_RGB565:
        case GPU_TF_RGB5A3:
        case GPU_TF_RGBA8:
        case GPU_TF_Z16:
        case GPU_TF_Z24X8:
        case GPU_CTF_RA8:
        case GPU_CTF_RG8:
        case GPU_CTF_GB8:
        case GPU_CTF_Z16L:
            shiftX = 2;
            shiftY = 2;
            break;
        default:
            shiftX = 0;
            shiftY = 0;
            break;
        }
        const u32 tileBytes = format == GPU_TF_RGBA8 || format == GPU_TF_Z24X8 ? 64 : 32;
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
        // A cooked texture (nebula_cooked_texture.h): the record goes whole; its header says the rest.
        if (const NebulaTexture* pRecord = nebula_texture_record(pImage)) {
            NebulaTextureImage record;
            record.bytes = pRecord;
            record.size = pRecord->size;
            record.width = pRecord->width;
            record.height = pRecord->height;
            record.format = NEBULA_TEXTURE_IMAGE;
            record.levels = pRecord->levels;
            const NebulaHostServices* pHost = nebula_host();
            pHost->texture_image(pHost->user, &record);
            return;
        }
        // Linear texels the game wrote (nebula_cooked_texture.h) go as they are. Console-format bytes go nowhere: the
        // renderer draws texture records only (cooked, linear, or a capture's picture under its key).
        if (format < NEBULA_TEXTURE_LINEAR_L8 || format > NEBULA_TEXTURE_LINEAR_RGBA8) {
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
        pHost->texture_image(pHost->user, &image);
    }

    void init(GpuTexObj* pObj, void* pImage, u16 width, u16 height, GpuTexFmt format, GpuTexWrapMode wrapS,
              GpuTexWrapMode wrapT, GpuBool mipmap) {
        J3DNativeTexObj& rTex = texture(pObj);
        memset(&rTex, 0, sizeof(rTex));
        rTex.mImage = pImage;
        rTex.mWidth = width;
        rTex.mHeight = height;
        rTex.mFormat = static_cast< u8 >(format);
        rTex.mWrapS = static_cast< u8 >(wrapS);
        rTex.mWrapT = static_cast< u8 >(wrapT);
        rTex.mMagFilter = GPU_LINEAR;
        rTex.mMipmap = mipmap;
        rTex.mEdgeLod = GPU_TRUE;
        if (mipmap) {
            // A mipmap's levels down to 1x1: the larger side's power of two.
            u32 levels = 0;
            for (u32 side = width > height ? width : height; side > 1; side >>= 1) {
                levels++;
            }
            rTex.mMinFilter = isColorIndexed(format) ? GPU_LIN_MIP_NEAR : GPU_LIN_MIP_LIN;
            rTex.mMaxLod = static_cast< f32 >(levels);
        } else {
            rTex.mMinFilter = GPU_LINEAR;
        }
    }

    void initCI(GpuTexObj* pObj, void* pImage, u16 width, u16 height, GpuCITexFmt format, GpuTexWrapMode wrapS,
                GpuTexWrapMode wrapT, GpuBool mipmap, u32 tlutName) {
        init(pObj, pImage, width, height, static_cast< GpuTexFmt >(format), wrapS, wrapT, mipmap);
        texture(pObj).mPalette = 1;
        texture(pObj).mTlutName = tlutName;
    }

    void initLOD(GpuTexObj* pObj, GpuTexFilter minFilter, GpuTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias,
                 GpuBool biasClamp, GpuBool edgeLod, GpuAnisotropy maxAniso) {
        // GpuInitTexObjLOD's clamps and steps: the bias in 1/32 (-4 to 3.99), the LOD range in 1/16 (0 to 10).
        if (lodBias < -4.0f) {
            lodBias = -4.0f;
        } else if (lodBias >= 4.0f) {
            lodBias = 3.99f;
        }
        minLod = minLod < 0.0f ? 0.0f : minLod > 10.0f ? 10.0f : minLod;
        maxLod = maxLod < 0.0f ? 0.0f : maxLod > 10.0f ? 10.0f : maxLod;
        J3DNativeTexObj& rTex = texture(pObj);
        rTex.mMinFilter = static_cast< u8 >(minFilter);
        rTex.mMagFilter = magFilter == GPU_LINEAR ? GPU_LINEAR : GPU_NEAR;
        rTex.mLodBias = static_cast< s8 >(static_cast< s32 >(lodBias * 32.0f)) / 32.0f;
        rTex.mMinLod = static_cast< u8 >(static_cast< s32 >(minLod * 16.0f)) / 16.0f;
        rTex.mMaxLod = static_cast< u8 >(static_cast< s32 >(maxLod * 16.0f)) / 16.0f;
        rTex.mBiasClamp = biasClamp;
        rTex.mEdgeLod = edgeLod;
        rTex.mMaxAniso = static_cast< u8 >(maxAniso);
    }

    void load(const GpuTexObj* pObj, GpuTexMapID map) {
        const J3DNativeTexObj& rTex = texture(pObj);
        NebulaStateOp ops[2];
        J3DStateList list(ops, 2);
        list.setTexLookupMode(map, static_cast< GpuTexWrapMode >(rTex.mWrapS), static_cast< GpuTexWrapMode >(rTex.mWrapT),
                              static_cast< GpuTexFilter >(rTex.mMinFilter), static_cast< GpuTexFilter >(rTex.mMagFilter),
                              rTex.mMinLod, rTex.mMaxLod, rTex.mLodBias, rTex.mBiasClamp, rTex.mEdgeLod,
                              static_cast< GpuAnisotropy >(rTex.mMaxAniso));
        list.setTexImgPtr(map, rTex.mImage);
        list.send();
    }

    void getAll(const GpuTexObj* pObj, void** ppImage, u16* pWidth, u16* pHeight, GpuTexFmt* pFormat,
                GpuTexWrapMode* pWrapS, GpuTexWrapMode* pWrapT, GpuBool* pMipmap) {
        const J3DNativeTexObj& rTex = texture(pObj);
        *ppImage = const_cast< void* >(rTex.mImage);
        *pWidth = rTex.mWidth;
        *pHeight = rTex.mHeight;
        *pFormat = static_cast< GpuTexFmt >(rTex.mFormat);
        *pWrapS = static_cast< GpuTexWrapMode >(rTex.mWrapS);
        *pWrapT = static_cast< GpuTexWrapMode >(rTex.mWrapT);
        *pMipmap = rTex.mMipmap;
    }

    void getLODAll(const GpuTexObj* pObj, GpuTexFilter* pMinFilter, GpuTexFilter* pMagFilter, f32* pMinLod, f32* pMaxLod,
                   f32* pLodBias, GpuBool* pBiasClamp, GpuBool* pEdgeLod, GpuAnisotropy* pMaxAniso) {
        const J3DNativeTexObj& rTex = texture(pObj);
        *pMinFilter = static_cast< GpuTexFilter >(rTex.mMinFilter);
        *pMagFilter = static_cast< GpuTexFilter >(rTex.mMagFilter);
        *pMinLod = rTex.mMinLod;
        *pMaxLod = rTex.mMaxLod;
        *pLodBias = rTex.mLodBias;
        *pBiasClamp = rTex.mBiasClamp;
        *pEdgeLod = rTex.mEdgeLod;
        *pMaxAniso = static_cast< GpuAnisotropy >(rTex.mMaxAniso);
    }

    u32 getTlut(const GpuTexObj* pObj) {
        return texture(pObj).mTlutName;
    }

    void setTlut(GpuTexObj* pObj, u32 tlutName) {
        texture(pObj).mTlutName = tlutName;
    }

    void initTlut(GpuTlutObj* pObj, void* pLut, GpuTlutFmt format, u16 entries) {
        J3DNativeTlutObj& rTlut = *reinterpret_cast< J3DNativeTlutObj* >(pObj);
        memcpy(rTlut.mLut, &pLut, sizeof(pLut));
        rTlut.mFormat = static_cast< u8 >(format);
        rTlut.mEntries = entries;
    }

};  // namespace J3DNativeTexture

void J3DNativeTextureInit(GpuTexObj* pObj, void* pImage, u16 width, u16 height, GpuTexFmt format, GpuTexWrapMode wrapS,
                          GpuTexWrapMode wrapT, GpuBool mipmap) {
    J3DNativeTexture::init(pObj, pImage, width, height, format, wrapS, wrapT, mipmap);
}

void J3DNativeTextureInitLOD(GpuTexObj* pObj, GpuTexFilter minFilter, GpuTexFilter magFilter, f32 minLod, f32 maxLod,
                             f32 lodBias, GpuBool biasClamp, GpuBool edgeLod, GpuAnisotropy maxAniso) {
    J3DNativeTexture::initLOD(pObj, minFilter, magFilter, minLod, maxLod, lodBias, biasClamp, edgeLod, maxAniso);
}

void J3DNativeTextureLoad(const GpuTexObj* pObj, GpuTexMapID map) {
    J3DNativeTexture::load(pObj, map);
}

void J3DNativeTextureRegisterImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels) {
    J3DNativeTexture::registerImage(pImage, format, width, height, levels);
}
