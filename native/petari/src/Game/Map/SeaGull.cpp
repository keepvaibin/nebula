#include "Game/Map/SeaGull.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHoverFront_f1, "exeHoverFront.f1", 0.995f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHoverFront_f2, "exeHoverFront.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHoverLeft_f1, "exeHoverLeft.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHoverRight_f1, "exeHoverRight.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f4, "control.f4", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f1, "updateHover.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f2, "updateHover.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f3, "updateHover.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f4, "updateHover.f4", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f5, "updateHover.f5", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f6, "updateHover.f6", 0.04f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f7, "updateHover.f7", 0.005f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f8, "updateHover.f8", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHover_f9, "updateHover.f9", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);

namespace NrvSeaGull {
    NEW_NERVE(SeaGullNrvHoverFront, SeaGull, HoverFront);
    NEW_NERVE(SeaGullNrvHoverLeft, SeaGull, HoverLeft);
    NEW_NERVE(SeaGullNrvHoverRight, SeaGull, HoverRight);
};  // namespace NrvSeaGull

SeaGull::SeaGull(SeaGullGroup* pGroup)
    : LiveActor("カモメ"), mSeaGullGroup(pGroup), mPointIndex(), mIsReverse(), mTargetUpdateTimer(MR::getRandom(0, 180)), mTargetPosition(),
      mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mBankedUp(0.0f, 1.0f, 0.0f), mSide(1.0f, 0.0f, 0.0f), mHoverTimer(), mBankAngle(),
      mGlideTimer(), mLiftTimer(), mChirpTimer() {
    lumacore::setClass(this, lumacore::kClassId< SeaGull >);
}

void SeaGull::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::calcActorAxis(&mSide, &mBankedUp, &mFront, this);
    MR::calcActorAxisY(&mUp, this);
    initModelManagerWithAnm("SeaGull", nullptr, false);
    MR::startBck(this, "Fly");
    MR::setBckFrameAtRandom(this);
    MR::connectToSceneEnvironment(this);
    f32 railLength = MR::getRailTotalLength(mSeaGullGroup);
    f32 maxRailCoord = railLength - 1.0f;
    f32 railCoord = MR::getRandom(1.0f, maxRailCoord);
    mPointIndex = railCoord / cNum_init_f1;
    MR::calcRailPosAtCoord(&mPosition, mSeaGullGroup, railCoord);
    mIsReverse = MR::isHalfProbability();
    mTargetPosition = mSeaGullGroup->updatePosInfo(&mPointIndex, mIsReverse);

    Vec3f side(mSide);
    Vec3f forward(mFront);
    f32 scale = MR::getRandom(-1.0f, 1.0f);
    side.scale(scale);
    scale = MR::getRandom(-1.0f, 1.0f);
    forward.scale(scale);
    mFront.set(side);
    mFront.add(forward);

    if (MR::isNearZero(mFront)) {
        mFront.x = 0.0f;
        mFront.y = 0.0f;
        mFront.z = 1.0f;
    } else {
        MR::normalize(&mFront);
    }

    initNerve(GET_NERVE(SeaGull, SeaGullNrvHoverFront));
    initSound(4, false);
    mChirpTimer = MR::getRandom(60, 480);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void SeaGull::exeHoverFront() {
    if (MR::isFirstStep(this)) {
        mHoverTimer = MR::getRandom(0, 60);
    }

    mBankAngle *= cNum_exeHoverFront_f1;

    if (mHoverTimer >= 0) {
        mHoverTimer--;
    } else {
        Vec3f forward(*mTargetPosition);
        forward.sub(mPosition);

        if (mPosition.distance(*mTargetPosition) > cNum_exeHoverFront_f2) {
            f32 prod = mSide.dot(forward);

            if (prod > 0.0f) {
                setNerve(GET_NERVE(SeaGull, SeaGullNrvHoverLeft));
            } else {
                setNerve(GET_NERVE(SeaGull, SeaGullNrvHoverRight));
            }
        }
    }
}

void SeaGull::exeHoverLeft() {
    if (MR::isFirstStep(this)) {
        mHoverTimer = MR::getRandom(60, 120);
    }

    mBankAngle -= cNum_exeHoverLeft_f1;

    if (MR::isStep(this, mHoverTimer)) {
        setNerve(GET_NERVE(SeaGull, SeaGullNrvHoverFront));
    }
}

void SeaGull::exeHoverRight() {
    if (MR::isFirstStep(this)) {
        mHoverTimer = MR::getRandom(60, 120);
    }

    mBankAngle += cNum_exeHoverRight_f1;

    if (MR::isStep(this, mHoverTimer)) {
        setNerve(GET_NERVE(SeaGull, SeaGullNrvHoverFront));
    }
}

void SeaGull::control() {
    updateHover();

    mVelocity.mult(cNum_control_f1);

    f32 mag = mVelocity.length();

    if (mag > cNum_control_f2) {
        MR::normalize(&mVelocity);
        mVelocity.mult(cNum_control_f3);
    }

    mSide.cross(mBankedUp, mFront);
    MR::normalize(&mSide);
    mBankedUp.cross(mFront, mSide);
    MR::normalize(&mBankedUp);
    mTargetUpdateTimer--;

    if (mTargetUpdateTimer <= 0) {
        mTargetPosition = mSeaGullGroup->updatePosInfo(&mPointIndex, mIsReverse);
        mTargetUpdateTimer = 0xB4;
    }

    if (mChirpTimer <= 0) {
        MR::startSound(this, "SE_OJ_SEAGULL_CHIRP");
        mChirpTimer = MR::getRandom(60, 480);
    } else {
        mChirpTimer--;
    }

    if (MR::isJudgedToClipFrustum300m(mPosition, cNum_control_f4)) {
        if (!MR::isHiddenModel(this)) {
            MR::hideModel(this);
        }
    } else {
        if (MR::isHiddenModel(this)) {
            MR::showModel(this);
        }
    }
}

void SeaGull::updateHover() {
    if (MR::abs(mBankAngle) > cNum_updateHover_f1) {
        mUp.set(mGravity);
        mUp.mult(-1.0f);
        mBankAngle = MR::clamp(mBankAngle, -cNum_updateHover_f2, cNum_updateHover_f3);

        Pos3f rotation;
        rotation.identity();
        const f32 angle = mBankAngle;
        rotation.setRotate(mFront, PI_180 * angle);
        mBankedUp.set(mUp);
        rotation.mult(mBankedUp, mBankedUp);
        rotation.setRotate(mUp, -(PI_180 * (cNum_updateHover_f4 * mBankAngle)));
        rotation.mult(mFront, mFront);
    }

    Vec3f forward(mFront);
    forward.scale(cNum_updateHover_f5);
    mVelocity.add(forward);

    if (mLiftTimer > 0) {
        Vec3f lift(mBankedUp);
        lift.scale(cNum_updateHover_f6);
        mVelocity.add(lift);
        mLiftTimer--;

        if (mLiftTimer <= 0) {
            mGlideTimer = MR::getRandom(60, 300);
        }
    } else {
        mVelocity.y -= cNum_updateHover_f7;
        Vec3f toTarget(*mTargetPosition);
        toTarget.sub(mPosition);

        f32 height = toTarget.dot(mUp);
        if (height < cNum_updateHover_f8) {
            mGlideTimer = NebulaFrameClock::stepsOfSeconds(5);
        } else {
            mGlideTimer--;

            if (height > cNum_updateHover_f9 || mGlideTimer <= 0) {
                mLiftTimer = MR::getRandom(30, 180);
            }
        }
    }
}

void SeaGull::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.identity();
    mtx.mMtx[0][0] = mSide.x;
    mtx.mMtx[1][0] = mSide.y;
    mtx.mMtx[2][0] = mSide.z;
    mtx.mMtx[0][1] = mBankedUp.x;
    mtx.mMtx[1][1] = mBankedUp.y;
    mtx.mMtx[2][1] = mBankedUp.z;
    mtx.mMtx[0][2] = mFront.x;
    mtx.mMtx[1][2] = mFront.y;
    mtx.mMtx[2][2] = mFront.z;
    mtx.mMtx[0][3] = mPosition.x;
    mtx.mMtx[1][3] = mPosition.y;
    mtx.mMtx[2][3] = mPosition.z;
    MR::setBaseTRMtx(this, mtx);
}

SeaGullGroup::SeaGullGroup(const char* pName) : LiveActor(pName), mPointCount(), mPoints() {
    lumacore::setClass(this, lumacore::kClassId< SeaGullGroup >);
}

void SeaGullGroup::init(const TableIter& rIter) {
    s32 birdCount = 10;
    MR::getPlacementArg0NoInit(rIter, &birdCount);
    initRailRider(rIter);
    mPosition.set(MR::getRailPos(this));
    mPointCount = static_cast< s32 >(MR::getRailTotalLength(this) / cNum_init_f2) + 1;
    f32 interval = MR::getRailTotalLength(this) / mPointCount;
    mPoints = new Vec3f[mPointCount];

    for (s32 i = 0; i < mPointCount; i++) {
        MR::calcRailPosAtCoord(&mPoints[i], this, interval * i);
    }

    for (s32 i = 0; i < birdCount; i++) {
        SeaGull* pSeaGull = new SeaGull(this);
        pSeaGull->init(rIter);
    }

    MR::invalidateClipping(this);
    makeActorAppeared();
}

Vec3f* SeaGullGroup::updatePosInfo(s32* pPointIndex, bool isReverse) const {
    if (isReverse) {
        (*pPointIndex)--;

        if (*pPointIndex <= 0) {
            *pPointIndex = mPointCount - 1;
        }
    } else {
        s32 next = *pPointIndex + 1;
        *pPointIndex = next;

        if (next >= mPointCount) {
            *pPointIndex = 0;
        }
    }

    return &mPoints[*pPointIndex];
}
