#include "Game/NPC/MiiFaceIcon.hpp"
#include "Game/Mii/MiiHead.hpp"
#include "Game/NPC/MiiFaceIconHolder.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_cooked_texture.h>
#include "Game/System/LumaCore.hpp"

MiiFaceIcon::MiiFaceIcon(u16 width, u16 height, const MiiFaceRecipe& rRecipe, const char* pName)
    : NameObj(pName), mImageBuffer(), mIsFavoriteColorBG(), mIsRequestMakeIcon(), mIsCreated(), mRecipe(rRecipe), mWidth(width),
      mHeight(height) {
    lumacore::setClass(this, lumacore::kClassId< MiiFaceIcon >);
    createImageBuffer();
    MR::registerMiiFaceIcon(this);
}

void MiiFaceIcon::init(const TableIter& /*rIter*/) {
}

void MiiFaceIcon::movement() {
}

void MiiFaceIcon::drawIcon() {
    if (mIsCreated) {
        return;
    }
    // The picture is named by the image after the header (createImageBuffer: mImageDataOffset), which the layout's
    // texture reads.
    const u8* const pKey = mImageBuffer + sizeof(ResTIMG);
    Mii::Character mii;
    if (mRecipe.character(&mii)) {
        const NebulaColor8 black = {0, 0, 0, 255};
        Mii::makeIcon(pKey, mii, mWidth, mHeight, mIsFavoriteColorBG ? nullptr : &black);
    }
    mIsCreated = true;
    mIsRequestMakeIcon = false;
}

void MiiFaceIcon::requestToMakeIcon() {
    mIsRequestMakeIcon = true;
}

void MiiFaceIcon::setIndex(u32 index) {
    mRecipe.mRef = Mii::Ref{Mii::SourceLibrary, index};
    mIsCreated = false;
}

// The header and a zeroed key after it: the picture is the renderer's, so no texels live here.
void MiiFaceIcon::createImageBuffer() {
    const u32 size = sizeof(ResTIMG) + NEBULA_TEXTURE_HEADER;
    mImageBuffer = new u8[size];
    MR::zeroMemory(mImageBuffer, size);

    ResTIMG* pHeader = reinterpret_cast< ResTIMG* >(mImageBuffer);
    pHeader->mWidth = mWidth;
    pHeader->mHeight = mHeight;
    pHeader->mWrapS = NEBULA_WRAP_CLAMP;
    pHeader->mWrapT = NEBULA_WRAP_CLAMP;
    pHeader->mImage = nullptr;
    pHeader->mMipmap = false;
    pHeader->mMaxAnisotropy = 0;
    pHeader->mMinType = NEBULA_FILTER_LINEAR;
    pHeader->mMagType = NEBULA_FILTER_LINEAR;
    pHeader->mMinLod = 0;
    pHeader->mMaxLod = 0;
    pHeader->mImageNum = 1;
    pHeader->mLodBias = 0;
    pHeader->mImageDataOffset = sizeof(ResTIMG);
}
