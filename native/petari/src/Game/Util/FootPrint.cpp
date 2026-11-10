#include "Game/Util/FootPrint.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <NebulaGpuTypes.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initMember_f1, "initMember.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMember_f2, "initMember.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMember_f3, "initMember.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMember_f4, "initMember.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 5.0f);

namespace {
    NEBULA_TUNABLE(s32, sFadeBufferCount, 10);
    static const s32 sFadeOutTime = NebulaFrameClock::stepsOfSeconds(1);
};  // namespace

FootPrint::FootPrint(const char* pName, s32 amount, s32 drawType) : NameObj(pName) {
    initMember(amount, drawType);
}

FootPrint::FootPrint(const char* pName, s32 amount) : NameObj(pName) {
    initMember(amount, MR::DrawType_FootPrint);
}

void FootPrint::initMember(s32 amount, s32 drawType) {
    mTexture = nullptr;
    mPrints = nullptr;
    mPrintMaxNum = 0;
    mPrintNum = 0;
    mRotatingPrintIndex = 0;
    mIsFading = false;

    mSideOffset = cNum_initMember_f1;
    mHeight = cNum_initMember_f2;
    mWidth = cNum_initMember_f3;
    mMinPrintDistance = cNum_initMember_f4;

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
    NebulaArena::CurrentScope scope(Arenas::scene());
    mTexture = new GameTexture(img);
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

bool FootPrint::addPrint(const Vec3f& rPos, const Vec3f& rUp, const Vec3f& rNormal, bool isFlipX) {
    mIsFading = false;

    if (mPrintNum > 0 && rPos.distance(mLastPrintPos) < mMinPrintDistance) {
        return false;
    }

    mLastPrintPos = rPos;
    mPrints[mRotatingPrintIndex].mPos = rPos;
    mPrints[mRotatingPrintIndex].mUp = rUp;
    mPrints[mRotatingPrintIndex].mNormal = rNormal;
    mPrints[mRotatingPrintIndex].mTimeLeft = ::sFadeOutTime;
    mPrints[mRotatingPrintIndex].mIsValid = true;
    mPrints[mRotatingPrintIndex].mIsFlipX = isFlipX;

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

        color.a = (s32)(cNum_draw_f1 * MR::normalize(mPrints[i].mTimeLeft, 0.0f, ::sFadeOutTime));
        material.setColor(0, color.r, color.g, color.b, color.a);

        Vec3f cross = mPrints[i].mUp.cross(mPrints[i].mNormal);
        // Prints alternate sides; each sits 5 units off the ground.
        Vec3f printPos;
        if ((i & 1) != 0) {
            printPos = mPrints[i].mPos + cross * mSideOffset;
        } else {
            printPos = mPrints[i].mPos - cross * mSideOffset;
        }

        printPos += mPrints[i].mNormal * cNum_draw_f2;

        TypedDraw site = TDDraw::site(material, 0);
        // An expired print draws nothing; each print keeps its own number so the prints after it pair with themselves.
        site.setPart(static_cast< u32 >(i));
        TDDraw::drawTexture3D(site, printPos, mPrints[i].mNormal, mPrints[i].mUp, mWidth, mHeight, nullptr, mPrints[i].mIsFlipX, false);
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
    return mPrints[index % mPrintMaxNum].mIsValid;
}
