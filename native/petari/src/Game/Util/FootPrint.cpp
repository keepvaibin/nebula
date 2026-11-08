#include "Game/Util/FootPrint.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <NebulaGpuTypes.h>

namespace {
    static const s32 sFadeBufferCount = 10;
    static const s32 sFadeOutTime = 60;
};  // namespace

FootPrint::FootPrint(const char* pName, s32 amount, s32 drawType) : NameObj(pName) {
    initMember(amount, drawType);
}

FootPrint::FootPrint(const char* pName, s32 amount) : NameObj(pName) {
    initMember(amount, MR::DrawType_0x17);
}

void FootPrint::initMember(s32 amount, s32 drawType) {
    mTexture = nullptr;
    mPrints = nullptr;
    mPrintMaxNum = 0;
    mPrintNum = 0;
    mRotatingPrintIndex = 0;
    mIsFading = false;

    mSideOffset = 20.0f;
    mHeight = 20.0f;
    mWidth = 20.0f;
    mMinPrintDistance = 80.0f;

    mLastPrintPos.zero();

    mPrints = new FootPrintInfo[amount];
    mPrintMaxNum = amount;

    for (int i = 0; i < mPrintMaxNum; i++) {
        mPrints[i].mIsValid = false;
    }

    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, drawType);
}

FootPrintInfo::FootPrintInfo() {
}

void FootPrint::setTexture(ResTIMG* img) {
    MR::CurrentHeapRestorer heapRestorer = MR::CurrentHeapRestorer(reinterpret_cast< NebulaArena* >(MR::getSceneHeap()));
    mTexture = new JUTTexture(img);
}

void FootPrint::movement() {
    if (mIsFading) {
        for (int i = 0; i < mPrintNum; i++) {
            if (!mPrints[i].mIsValid) {
                continue;
            }

            mPrints[i].mTimeLeft--;

            if (mPrints[i].mTimeLeft <= 0) {
                mPrints[i].mIsValid = false;
            }
        }

        return;
    }

    for (s32 i = mRotatingPrintIndex; i < mRotatingPrintIndex + ::sFadeBufferCount; i++) {
        s32 rotatedIndex = i;
        if (i >= mPrintMaxNum) {
            rotatedIndex -= mPrintMaxNum;
        }

        if (!mPrints[rotatedIndex].mIsValid) {
            continue;
        }

        mPrints[rotatedIndex].mTimeLeft--;

        if (mPrints[rotatedIndex].mTimeLeft <= 0) {
            mPrints[rotatedIndex].mIsValid = false;
        }
    }
}

bool FootPrint::addPrint(const Vec3f& rPos, const Vec3f& rArg2, const Vec3f& rArg3, bool arg4) {
    mIsFading = false;

    if (mPrintNum > 0 && rPos.distance(mLastPrintPos) < mMinPrintDistance) {
        return false;
    }

    mLastPrintPos = rPos;
    mPrints[mRotatingPrintIndex].mPos = rPos;
    mPrints[mRotatingPrintIndex].mUp = rArg2;
    mPrints[mRotatingPrintIndex].mNormal = rArg3;
    mPrints[mRotatingPrintIndex].mTimeLeft = ::sFadeOutTime;
    mPrints[mRotatingPrintIndex].mIsValid = true;
    mPrints[mRotatingPrintIndex].mIsFlipX = arg4;

    mRotatingPrintIndex++;
    mPrintNum++;

    if (mRotatingPrintIndex >= mPrintMaxNum) {
        mRotatingPrintIndex -= mPrintMaxNum;
    }

    if (mPrintNum > mPrintMaxNum) {
        mPrintNum = mPrintMaxNum;
    }

    return true;
}

void FootPrint::draw() const {
    if (mPrintNum <= 0) {
        return;
    }

    SiteMaterial material;
    TDDraw::setupNative(&material, 2, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.setTexture(0, mTexture);

    Color8 color(0, 0, 0, 255);

    for (int i = 0; i < mPrintNum; i++) {
        if (!mPrints[i].mIsValid) {
            continue;
        }

        color.a = (s32)(255.0f * MR::normalize(mPrints[i].mTimeLeft, 0.0f, ::sFadeOutTime));
        material.setColor(0, color.r, color.g, color.b, color.a);

        Vec3f cross = mPrints[i].mUp.cross(mPrints[i].mNormal);
        Vec3f v1;
        if ((i & 1) != 0) {
            v1 = mPrints[i].mPos + cross * mSideOffset;
        } else {
            v1 = mPrints[i].mPos - cross * mSideOffset;
        }

        v1 += mPrints[i].mNormal * 5.0f;

        TypedDraw site = TDDraw::site(material, 0);
        TDDraw::drawTexture3D(site, v1, mPrints[i].mNormal, mPrints[i].mUp, mWidth, mHeight, nullptr, mPrints[i].mIsFlipX != false, false);
    }

    TDDraw::close();
}

void FootPrint::clear() {
    mIsFading = true;
}

void FootPrint::clearForce() {
    mPrintNum = 0;
    mRotatingPrintIndex = 0;
}

Vec3f* FootPrint::getPrintPos(u32 index) const {
    return &mPrints[index % mPrintMaxNum].mPos;
}

void FootPrint::invalidate(u32 index) {
    mPrints[index % mPrintMaxNum].mIsValid = false;
}

bool FootPrint::isValid(u32 index) const {
    return mPrints[index % mPrintMaxNum].mIsValid != false;
}
