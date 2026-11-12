#include "Game/Boss/TripodBossBreakMovement.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SpringValue.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sBreakRandamaize = 1.0f;
    const f32 sMinStartMoment = 0.01f;
    const f32 sMaxStartMoment = 0.04f;
    const f32 sGravityAccel = 0.2f;
};  // namespace

namespace NrvTripodBossBreakMovement {
    NEW_NERVE(TripodBossBreakMovementNrvWait, TripodBossBreakMovement, Wait);
    NEW_NERVE(TripodBossBreakMovementNrvStartBreak, TripodBossBreakMovement, StartBreak);
    NEW_NERVE(TripodBossBreakMovementNrvBreak, TripodBossBreakMovement, Break);
};  // namespace NrvTripodBossBreakMovement

TripodBossBreakMovement::TripodBossBreakMovement(const char* pName)
    : LiveActor(pName), mSpring(), mMoment(0, 0, 0), mBreakVelocity(0, 0, 0), mFlyDir(0, 1, 0), mFlyDirRandomRate(0.3f), mFlySpeed(), mDamping(0.9f), mJointIndex(-1) {
    lumacore::setClass(this, lumacore::kClassId< TripodBossBreakMovement >);
    mMtx.identity();
    initNerve(GET_NERVE(TripodBossBreakMovement, TripodBossBreakMovementNrvWait));
    MR::invalidateClipping(this);
    mSpring = new SpringValue();
    mSpring->setParam(0.0f, 0.0f, 0.2f, 0.9f, 0.0f);
    setBreakDownLevel(1);
}

void TripodBossBreakMovement::start(const Pos3f& rMtx, s32 jointIndex) {
    mMtx.set(rMtx);
    mMtx.getTrans(mPos);

    mMoment.set< f32 >(MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize), MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize),
                   MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize));
    MR::normalizeOrZero(&mMoment);
    mMoment *= MR::getRandom(::sMinStartMoment, ::sMaxStartMoment);

    mDamping = MR::getRandom(0.995f, 0.997f);

    MR::zeroVelocity(this);

    mJointIndex = jointIndex;

    Pos3f jointMtx;
    MR::getTripodBossJointMatrix(&jointMtx, mJointIndex);
    jointMtx.getYDir(mFlyDir);

    Vec3f jitter;
    jitter.set< f32 >(MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize), MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize),
                      MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize));
    mFlyDir += jitter * mFlyDirRandomRate;

    mSpring->mVelocity += MR::getRandom(20.0f, 25.0f);

    setNerve(GET_NERVE(TripodBossBreakMovement, TripodBossBreakMovementNrvStartBreak));
    MR::invalidateClipping(this);
    appear();
}

void TripodBossBreakMovement::movement() {
    LiveActor::movement();
}

void TripodBossBreakMovement::setBreakDownLevel(s32 level) {
    if (level < 0) {
        level = 0;
    }

    if (level >= 3) {
        level = 3;
    }

    switch (level) {
    case 0:
    case 1:
    case 2:
        mFlySpeed = MR::getRandom(50.0f, 90.0f);
        break;
    case 3:
        mFlySpeed = 50.0f;
        mFlyDirRandomRate = 0.0f;
        break;
    }
}

void TripodBossBreakMovement::exeWait() {
}

void TripodBossBreakMovement::exeStartBreak() {
    Vec3f yDir;
    mMtx.getYDir(yDir);
    mMtx.setTrans(mPos + yDir * mSpring->mSpringValue);

    mSpring->update();

    if (MR::isGreaterStep(this, 0)) {
        setNerve(GET_NERVE(TripodBossBreakMovement, TripodBossBreakMovementNrvBreak));
    }
}

void TripodBossBreakMovement::exeBreak() {
    MR::rotateMtxMoment(&mMtx, mMtx, mMoment);

    if (MR::isFirstStep(this)) {
        mMtx.getTrans(mPos);
        mBreakVelocity += mFlyDir * mFlySpeed;
    }

    mPos += mBreakVelocity;
    mMtx.setTrans(mPos);

    Vec3f gravity;
    MR::calcGravityVector(this, mPos, &gravity, nullptr, MR::getTripodBossGravityHost());

    mBreakVelocity += gravity * ::sGravityAccel;
    mBreakVelocity *= mDamping;
}
