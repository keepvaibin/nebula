#include "Game/NPC/MiiFaceIcon.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/MiiFaceIconHolder.hpp"
#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

MiiFaceIcon::MiiFaceIcon(u16 width, u16 height, const MiiFaceRecipe& rRecipe, const char* pName)
    : NameObj(pName), mIndex(), mIsFavoriteColorBG(), mWidth(width), mHeight(height), mRecipe(new MiiFaceRecipe(rRecipe)), mImageBuffer(),
      mIsCreated(), mMakeIconResult(MiiErrcode_Unknown), mIsRequestMakeIcon() {
    createImageBuffer();
    MR::registerMiiFaceIcon(this);
}

void MiiFaceIcon::init(const JMapInfoIter& rIter) {
}

void MiiFaceIcon::movement() {
}

void MiiFaceIcon::drawIcon() {
    if (mIsCreated) {
        return;
    }

    // Into the image, after its ResTIMG header (createImageBuffer: mImageDataOffset): the layout's TexMap reads the
    // image at the header plus 32.
    u8* const image = mImageBuffer + sizeof(ResTIMG);
    if (mIsFavoriteColorBG) {
        mMakeIconResult = mRecipe->makeIconWithFavoriteColor(image, mWidth, mHeight);
    } else {
        mMakeIconResult = mRecipe->makeIcon(image, mWidth, mHeight, Color8(0, 0, 0, 255));
    }

    mIsCreated = true;
    mIsRequestMakeIcon = false;
}

void MiiFaceIcon::requestToMakeIcon() {
    mIsRequestMakeIcon = true;
}

void MiiFaceIcon::setIndex(u16 index) {
    mIndex = index;
    mRecipe->mIndex = index;
    mIsCreated = false;
    mMakeIconResult = MiiErrcode_Unknown;
}

void MiiFaceIcon::createImageBuffer() {
    ResTIMG* pImageBuffer;
    u16 width;
    u16 height;
    u32 imageBufferSize = mWidth * mHeight * 2 + sizeof(ResTIMG);

    mImageBuffer = new (sizeof(ResTIMG)) u8[imageBufferSize];
    MR::zeroMemory(mImageBuffer, imageBufferSize);

    height = mHeight;
    width = mWidth;

    pImageBuffer = reinterpret_cast< ResTIMG* >(mImageBuffer);
    pImageBuffer->mWidth = width;
    pImageBuffer->mHeight = height;
    pImageBuffer->mWrapS = NEBULA_WRAP_CLAMP;
    pImageBuffer->mWrapT = NEBULA_WRAP_CLAMP;
    pImageBuffer->mImage = nullptr;
    pImageBuffer->mMipmap = false;
    pImageBuffer->mDoEdgeLod = false;
    pImageBuffer->mBiasClamp = false;
    pImageBuffer->mMaxAnisotropy = 0;
    pImageBuffer->mMinType = NEBULA_FILTER_LINEAR;
    pImageBuffer->mMagType = NEBULA_FILTER_LINEAR;
    pImageBuffer->mMinLod = 0;
    pImageBuffer->mMaxLod = 0;
    pImageBuffer->mImageNum = 1;
    pImageBuffer->mLodBias = 0;
    pImageBuffer->mImageDataOffset = sizeof(ResTIMG);
}
