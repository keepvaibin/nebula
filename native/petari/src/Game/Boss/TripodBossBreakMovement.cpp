#include "Game/Boss/TripodBossBreakMovement.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SpringValue.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_TripodBossBreakMovement_f1, "TripodBossBreakMovement.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_TripodBossBreakMovement_f2, "TripodBossBreakMovement.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 0.995f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f2, "start.f2", 0.997f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f3, "start.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f4, "start.f4", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBreakDownLevel_f1, "setBreakDownLevel.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBreakDownLevel_f2, "setBreakDownLevel.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setBreakDownLevel_f3, "setBreakDownLevel.f3", 50.0f);

namespace {
    NEBULA_TUNABLE(f32, sBreakRandamaize, 1.0f);
    NEBULA_TUNABLE(f32, sMinStartMoment, 0.01f);
    NEBULA_TUNABLE(f32, sMaxStartMoment, 0.04f);
    NEBULA_TUNABLE(f32, sGravityAccel, 0.2f);
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
    mSpring->setParam(0.0f, 0.0f, cNum_TripodBossBreakMovement_f1, cNum_TripodBossBreakMovement_f2, 0.0f);
    setBreakDownLevel(1);
}

void TripodBossBreakMovement::start(const Pos3f& rMtx, s32 jointIndex) {
    mMtx.set(rMtx);
    mMtx.getTrans(mPos);

    mMoment.set< f32 >(MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize), MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize),
                   MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize));
    MR::normalizeOrZero(&mMoment);
    mMoment *= MR::getRandom(::sMinStartMoment, ::sMaxStartMoment);

    mDamping = MR::getRandom(cNum_start_f1, cNum_start_f2);

    MR::zeroVelocity(this);

    mJointIndex = jointIndex;

    Pos3f jointMtx;
    MR::getTripodBossJointMatrix(&jointMtx, mJointIndex);
    jointMtx.getYDir(mFlyDir);

    Vec3f jitter;
    jitter.set< f32 >(MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize), MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize),
                      MR::getRandom(-::sBreakRandamaize, ::sBreakRandamaize));
    mFlyDir += jitter * mFlyDirRandomRate;

    mSpring->mVelocity += MR::getRandom(cNum_start_f3, cNum_start_f4);

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
        mFlySpeed = MR::getRandom(cNum_setBreakDownLevel_f1, cNum_setBreakDownLevel_f2);
        break;
    case 3:
        mFlySpeed = cNum_setBreakDownLevel_f3;
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
