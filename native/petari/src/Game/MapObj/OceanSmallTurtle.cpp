#include "Game/MapObj/OceanSmallTurtle.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/AreaObj/MercatorTransformCube.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFloatSignal_f1, "exeFloatSignal.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailMove_f1, "exeRailMove.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRailMove_f2, "exeRailMove.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_boundFloater_f1, "boundFloater.f1", 7.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_boundFloater_f2, "boundFloater.f2", 9.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_floatFloater_f1, "floatFloater.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_floatFloater_f2, "floatFloater.f2", 150.0f);

namespace NrvOceanSmallTurtle {
    NEW_NERVE(OceanSmallTurtleNrvWait, OceanSmallTurtle, Wait);
    NEW_NERVE(OceanSmallTurtleNrvFloatSignal, OceanSmallTurtle, FloatSignal);
    NEW_NERVE(OceanSmallTurtleNrvFloatUp, OceanSmallTurtle, FloatUp);
    NEW_NERVE(OceanSmallTurtleNrvCatchRail, OceanSmallTurtle, CatchRail);
    NEW_NERVE(OceanSmallTurtleNrvRailMove, OceanSmallTurtle, RailMove);
};  // namespace NrvOceanSmallTurtle

namespace {
    NEBULA_TUNABLE(f32, sRisingSpeedAtFloatSignal, 0.5f);
    NEBULA_TUNABLE(f32, sCatchRailSpeed, 2.0f);
    NEBULA_TUNABLE(f32, sJudgeForCatchRail, 100.0f);
    NEBULA_TUNABLE(f32, sBoundAmpMax, 20.0f);
    static const s32 sBoundTime = NebulaFrameClock::stepsOfMilliseconds(2500);
    NEBULA_TUNABLE(f32, sStartFloatPower, 1.0f);
    NEBULA_TUNABLE(f32, sRambleSide, 1.0f);
    NEBULA_TUNABLE(f32, sRambleRotX, 1.0f);
    NEBULA_TUNABLE(f32, sDefaultAccelRate, 20.0f);
    NEBULA_TUNABLE(f32, sAccelMax, 40.0f);
    static const s32 sStepForFloatSignal = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sStepToRisingForFloatSignal = NebulaFrameClock::stepsOfSeconds(2);
};  // namespace

OceanSmallTurtle::OceanSmallTurtle(const char* pName)
    : LiveActor(pName), mRailMover(), mFloatUpSpeed(), mAccelRate(::sDefaultAccelRate), mStartNearestRailPos(0.0f), mFloatDelayTimer(), mArg1(),
      mBoundTimer(), mMercatorPos(0.0f), mBoundDisplacement(), mRailClippingPos(0.0f), mRailClippingRadius(), mStartRotationX(), mIsOnPlayer(),
      isBound(), isStartFloat() {
    mEffectMtx.identity();
}

void OceanSmallTurtle::init(const TableIter& rIter) {
    MR::getPlacementArg0NoInit(rIter, &mAccelRate);
    mAccelRate /= cNum_init_f1;

    MR::getPlacementArg1NoInit(rIter, &mArg1);
    MR::getPlacementRotate(rIter, &mInfoRotate);

    MR::initDefaultPosForMercator(this, rIter, true);
    mMercatorPos.set(mPosition);

    initModelManagerWithAnm("OceanSmallTurtle", nullptr, false);
    mEffectMtx.set(getBaseMtx());

    MR::connectToSceneCollisionMapObj(this);

    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);

    MR::initCollisionParts(this, "OceanSmallTurtle", getSensor("body"), nullptr);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initRailRider(rIter);

    MR::calcNearestRailPosForMercator(&mStartNearestRailPos, this, ::sJudgeForCatchRail);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f2);
    MR::calcRailClippingInfoForMercator(&mRailClippingPos, &mRailClippingRadius, this, cNum_init_f3, cNum_init_f4);
    mRailMover = MR::createMapPartsRailMoverForMercator(this, rIter, false);

    MR::onCalcGravity(this);

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor(this, &OceanSmallTurtle::startFloat));
    }

    MR::tryRegisterDemoCast(this, rIter);
    initNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvWait));
    makeActorAppeared();
}

void OceanSmallTurtle::control() {
    mIsOnPlayer = MR::isOnPlayer(getSensor(nullptr));

    if (isNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvFloatUp))) {
        floatFloater();
    }

    boundFloater();

    mMercatorPos.add(mVelocity);

    MR::makeMtxUpNoSupportPos(&mEffectMtx, -mGravity, mMercatorPos);
}

void OceanSmallTurtle::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.identity();
    mPosition.set(mMercatorPos);

    Pos3f rotateMtx;
    MR::makeMtxRotate(rotateMtx, mRotation);
    mtx.concat(rotateMtx);

    if (isNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvRailMove))) {
        Pos3f rotateMtx2;
        MR::makeMtxRotate(rotateMtx2, mInfoRotate);
        mtx.concat(rotateMtx2);
    }

    mtx.setTrans(mPosition);

    if (isBound) {
        Vec3f up(-mGravity);
        Vec3f scaledUp;
        scaledUp.scale(mBoundDisplacement, up);
        mtx[0][3] += scaledUp.x;
        mtx[1][3] += scaledUp.y;
        mtx[2][3] += scaledUp.z;
    }

    MR::setBaseTRMtx(this, mtx);
}

void OceanSmallTurtle::exeWait() {
    if (MR::isFirstStep(this)) {
        mStartRotationX = mRotation.x;
    }

    Vec3f up;
    up.negate(mGravity);
    Vec3f scaledUp;
    scaledUp.scale(mArg1, up);

    if (MR::isInWater(this, scaledUp)) {
        if (mFloatDelayTimer >= cNum_exeWait_f1) {
            setNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvFloatSignal));
        } else {
            mFloatDelayTimer += 1.0f;
        }
    }
}

void OceanSmallTurtle::exeFloatSignal() {
    f32 speedAndDir = cNum_exeFloatSignal_f1 * (getNerveStep() % 8 < 4 ? ::sRambleSide : -::sRambleSide);
    Vec3f sideVec;
    MR::calcSideVec(&sideVec, this);

    Vec3f velocity;

    velocity.scale(speedAndDir, sideVec);
    mVelocity.set(velocity);

    MR::startLevelSound(this, "SE_OJ_LV_O_SML_TURTLE_SIGN");

    mRotation.x += getNerveStep() % 12 < 6 ? ::sRambleRotX : -::sRambleRotX;

    if (MR::isGreaterStep(this, ::sStepForFloatSignal)) {
        mVelocity.scale(::sRisingSpeedAtFloatSignal, -mGravity);
    }

    if (MR::isStep(this, ::sStepToRisingForFloatSignal)) {
        setNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvFloatUp));
    }
}

void OceanSmallTurtle::exeFloatUp() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_O_SML_TURTLE_FLOAT_ST");
    }

    MR::startLevelSound(this, "SE_OJ_LV_O_SML_TURTLE_FLOAT");

    if (isEndSpringWater()) {
        MR::startSound(this, "SE_OJ_O_SML_TURTLE_FLOAT_ED");
        setNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvCatchRail));
        return;
    }

    if (isNeedsForBound()) {
        mBoundTimer = 0;
        isBound = true;
    }
}

void OceanSmallTurtle::exeCatchRail() {
    if (!catchNearestRailPos()) {
        return;
    }

    mVelocity.zero();
    mRailMover->start();

    MR::convertMercatorPlaneToSphereTransAndRotate(&mMercatorPos, &mRotation, mRailMover->mStartPos, true);

    setNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvRailMove));
}

void OceanSmallTurtle::exeRailMove() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::initAndSetRailClippingForMercator(&mRailClippingPos, this, cNum_exeRailMove_f1, cNum_exeRailMove_f2);
        MR::setClippingTypeSphere(this, mRailClippingRadius, &mRailClippingPos);
    }

    mRailMover->movement();
    MR::startLevelSound(this, "SE_OJ_LV_O_SML_TURTLE_MOVE");

    MR::convertMercatorPlaneToSphereTransAndRotate(&mMercatorPos, &mRotation, mRailMover->mStartPos, true);

    if (isNeedsForBound()) {
        mBoundTimer = 0;
        isBound = true;
    }
}

void OceanSmallTurtle::startFloat() {
    if (isNerve(GET_NERVE(OceanSmallTurtle, OceanSmallTurtleNrvWait))) {
        MR::invalidateClipping(this);
    }
}

bool OceanSmallTurtle::boundFloater() {
    if (!isBound) {
        return false;
    }

    if (mBoundTimer == 0) {
        MR::startSound(this, "SE_OJ_O_SML_TURTLE_RIPPLE");
    }

    if (mBoundTimer == ::sBoundTime) {
        isBound = false;
        mBoundDisplacement = 0.0f;
        mBoundTimer = 0;
        return true;
    }

    f32 boundAmp = (::sBoundAmpMax - (mBoundTimer * (1.0f / cNum_boundFloater_f1)));

    mBoundDisplacement = boundAmp * MR::sinDegree(mBoundTimer * cNum_boundFloater_f2);

    mBoundTimer++;
    return false;
}

bool OceanSmallTurtle::catchNearestRailPos() {
    if (mStartNearestRailPos.squared(mMercatorPos) < ::sJudgeForCatchRail) {
        return true;
    }

    Vec3f vec;
    vec.sub(mStartNearestRailPos, mMercatorPos);

    MR::normalize(&vec);

    mVelocity.scale(::sCatchRailSpeed, vec);
    return false;
}

void OceanSmallTurtle::floatFloater() {
    Vec3f up;
    up.negate(mGravity);
    Vec3f scaledUp;
    scaledUp.scale(mArg1, up);

    if (MR::isInWater(this, scaledUp)) {
        if (mFloatDelayTimer >= cNum_floatFloater_f1) {
            mVelocity.scale(::sStartFloatPower + mFloatUpSpeed, up);
            mFloatDelayTimer = cNum_floatFloater_f2;
            if (mFloatUpSpeed < ::sAccelMax) {
                mFloatUpSpeed += mAccelRate;
            }
        } else {
            mFloatDelayTimer += 1.0f;
        }
    } else {
        if (!isStartFloat) {
            MR::emitEffect(this, "Ripple");
            MR::setEffectHostMtx(this, "Ripple", mEffectMtx);
            mBoundTimer = 0;
            isBound = true;
        }

        isStartFloat = true;
        mFloatUpSpeed = 0.0f;
        mRotation.x = mStartRotationX;
        mVelocity.zero();
    }
}

bool OceanSmallTurtle::isNeedsForBound() {
    if (mIsOnPlayer && MR::isPlayerJumpRising() && MR::checkPlayerActionTrigger()) {
        return true;
    }

    if (!mIsOnPlayer && MR::isOnPlayer(getSensor(nullptr))) {
        return true;
    }

    return false;
}

bool OceanSmallTurtle::isEndSpringWater() {
    return mVelocity.isZero() && isStartFloat && !isBound;
}
