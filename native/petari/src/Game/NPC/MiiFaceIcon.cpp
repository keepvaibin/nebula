#include "Game/NPC/MiiFaceIcon.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/MiiFaceIconHolder.hpp"
#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
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

    // Into the image, after its ResTIMG header (createImageBuffer: mImageDataOffset). The console's code copies the
    // face to the header's address (802775F0 passes mImageBuffer); on the console the GPU's copy overwrites the
    // header, already read into the layout's TexMap, and the icon shows 32 bytes (one 4x4 block) out of place.
    // Natively the copy stays on the GPU at the address it was made to, so the TexMap's image (the header plus 32)
    // must be where the face goes.
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
    pImageBuffer->mFormat = GPU_TF_RGB5A3;
    pImageBuffer->mWidth = width;
    pImageBuffer->mHeight = height;
    pImageBuffer->mWrapS = GPU_CLAMP;
    pImageBuffer->mWrapT = GPU_CLAMP;
    pImageBuffer->mImage = nullptr;
    pImageBuffer->mMipmap = false;
    pImageBuffer->mDoEdgeLod = false;
    pImageBuffer->mBiasClamp = false;
    pImageBuffer->mMaxAnisotropy = GPU_ANISO_1;
    pImageBuffer->mMinType = GPU_LINEAR;
    pImageBuffer->mMagType = GPU_LINEAR;
    pImageBuffer->mMinLod = 0;
    pImageBuffer->mMaxLod = 0;
    pImageBuffer->mImageNum = 1;
    pImageBuffer->mLodBias = 0;
    pImageBuffer->mImageDataOffset = sizeof(ResTIMG);
}
