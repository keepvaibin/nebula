#include "JSystem/J3DGraphBase/J3DNativeTexture.hpp"
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace J3DNativeTexture {
    u32 linearSize(u8 format, u16 width, u16 height, u8 levels) {
        return nebula_texture_level_bytes(width, height, levels != 0 ? levels : 1, nebula_texture_linear_layout(format));
    }

    void registerRecord(const void* pImage) {
        const NebulaTexture* pRecord = nebula_texture_record(pImage);
        if (pRecord == nullptr) {
            return;
        }
        NebulaTextureImage record;
        record.bytes = pRecord;
        record.size = pRecord->size;
        record.width = pRecord->width;
        record.height = pRecord->height;
        record.format = NEBULA_TEXTURE_IMAGE;
        record.levels = pRecord->levels;
        const NebulaHostServices* pHost = nebula_host();
        pHost->texture_image(pHost->user, &record);
    }

    void registerLinear(const void* pImage, u8 format, u16 width, u16 height, u8 levels) {
        if (pImage == nullptr || width == 0 || height == 0) {
            return;
        }
        NebulaTextureImage image;
        image.bytes = pImage;
        image.size = linearSize(format, width, height, levels);
        image.width = width;
        image.height = height;
        image.format = format;
        image.levels = levels;
        const NebulaHostServices* pHost = nebula_host();
        pHost->texture_image(pHost->user, &image);
    }

    void init(J3DNativeTexObj* pObj, const void* pImage, u16 width, u16 height, u8 wrapS, u8 wrapT) {
        memset(pObj, 0, sizeof(*pObj));
        pObj->mImage = pImage;
        pObj->mWidth = width;
        pObj->mHeight = height;
        pObj->mWrapS = wrapS;
        pObj->mWrapT = wrapT;
        pObj->mMinFilter = NEBULA_FILTER_LINEAR;
        pObj->mMagFilter = NEBULA_FILTER_LINEAR;
        pObj->mEdgeLod = 1;
    }

    void setFilter(J3DNativeTexObj* pObj, u8 minFilter, u8 magFilter, f32 minLod, f32 maxLod, f32 lodBias, bool edgeLod,
                   u8 maxAniso) {
        pObj->mMinFilter = minFilter;
        pObj->mMagFilter = magFilter == NEBULA_FILTER_LINEAR ? NEBULA_FILTER_LINEAR : NEBULA_FILTER_NEAREST;
        pObj->mMinLod = minLod;
        pObj->mMaxLod = maxLod;
        pObj->mLodBias = lodBias;
        pObj->mEdgeLod = edgeLod;
        pObj->mMaxAniso = maxAniso;
    }
};  // namespace J3DNativeTexture

void J3DNativeTextureInit(J3DNativeTexObj* pObj, const void* pImage, u16 width, u16 height, u8 wrapS, u8 wrapT) {
    J3DNativeTexture::init(pObj, pImage, width, height, wrapS, wrapT);
}

void J3DNativeTextureSetFilter(J3DNativeTexObj* pObj, u8 minFilter, u8 magFilter, f32 minLod, f32 maxLod, f32 lodBias,
                               int edgeLod, u8 maxAniso) {
    J3DNativeTexture::setFilter(pObj, minFilter, magFilter, minLod, maxLod, lodBias, edgeLod != 0, maxAniso);
}

void J3DNativeTextureRegisterRecord(const void* pImage) {
    J3DNativeTexture::registerRecord(pImage);
}
