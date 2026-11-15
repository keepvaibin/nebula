#include "Game/MapObj/SpringWaterFloaterSpot.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Functor.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvSpringWaterFloaterSpot {
    NEW_NERVE(SpringWaterFloaterSpotNrvWait, SpringWaterFloaterSpot, Wait);
    NEW_NERVE(SpringWaterFloaterSpotNrvFloatSignal, SpringWaterFloaterSpot, FloatSignal);
    NEW_NERVE(SpringWaterFloaterSpotNrvFloatUp, SpringWaterFloaterSpot, FloatUp);
    NEW_NERVE(SpringWaterFloaterSpotNrvFloatEnd, SpringWaterFloaterSpot, FloatEnd);
};  // namespace NrvSpringWaterFloaterSpot

SpringWaterFloaterSpot::SpringWaterFloaterSpot(const char* pName)
    : LiveActor(pName), mRiseSpeed(), mRiseAccel(20.0f), mHeight(), mWaterCheckOffset(), mBoundStep(), mFloatPos(0.0f, 0.0f, 0.0f), mBoundSize(), mIsOnPlayer(), mNeedsBound(), mHasSurfaced(),
      mFloatTimer() {
    lumacore::setClass(this, lumacore::kClassId< SpringWaterFloaterSpot >);
}

void SpringWaterFloaterSpot::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    mInitialRotation = mRotation;

    const char* objectName;
    MR::getObjectName(&objectName, rIter);
    initModelManagerWithAnm(objectName, nullptr, false);

    mFloatMtx.set(getBaseMtx());

    MR::connectToSceneCollisionMapObj(this);

    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, objectName, getSensor("body"), nullptr);

    initEffectKeeper(0, nullptr);
    initSound(4, 0);

    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);

    MR::onCalcGravity(this);

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor(this, &SpringWaterFloaterSpot::startFloat));
    }

    MR::tryRegisterDemoCast(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mRiseAccel);
    mRiseAccel /= 100.0f;

    MR::getPlacementArg1NoInit(rIter, &mWaterCheckOffset);

    mFloatPos.set(mPosition);

    initNerve(GET_NERVE(SpringWaterFloaterSpot, SpringWaterFloaterSpotNrvWait));

    makeActorAppeared();
}

void SpringWaterFloaterSpot::control() {
    mIsOnPlayer = MR::isOnPlayer(getSensor(nullptr));

    if (isNerve(GET_NERVE(SpringWaterFloaterSpot, SpringWaterFloaterSpotNrvFloatUp))) {
        floatFloater();
    }

    boundFloater();

    mFloatPos.add(mVelocity);
    MR::makeMtxUpNoSupportPos(&mFloatMtx, -mGravity, mFloatPos);

    Vec3f waterCheckPos;
    Vec3f waterCheckOffset(0.0f, -70.0f + mWaterCheckOffset, 0.0f);
    NebulaMtxMultVec(mFloatMtx, waterCheckOffset, waterCheckPos);
    mFloatMtx.setTrans(waterCheckPos);
}

void SpringWaterFloaterSpot::calcAndSetBaseMtx() {
    mPosition.set(mFloatPos);

    Pos3f baseMtx;
    MR::makeMtxTR(baseMtx, this);

    if (mNeedsBound) {
        Vec3f up(-mGravity);
        Vec3f offset;
        offset.scale(mBoundSize, up);
        baseMtx[0][3] += offset.x;
        baseMtx[1][3] += offset.y;
        baseMtx[2][3] += offset.z;
    }

    MR::setBaseTRMtx(this, baseMtx);
}

void SpringWaterFloaterSpot::exeWait() {
    Vec3f up(-mGravity);
    Vec3f waterCheckPos;
    waterCheckPos.scale(mWaterCheckOffset, up);

    if (MR::isInWater(this, waterCheckPos)) {
        if (mHeight >= 90.0f) {
            setNerve(GET_NERVE(SpringWaterFloaterSpot, SpringWaterFloaterSpotNrvFloatSignal));
        } else {
            mHeight += 1.0f;
        }
    }
}

void SpringWaterFloaterSpot::exeFloatSignal() {
    f32 scale = 3.0f * (getNerveStep() % 8 < 4 ? 1.0f : -1.0f);

    Vec3f sideVec;
    MR::calcSideVec(&sideVec, this);

    Vec3f scaledSideVec;
    scaledSideVec.scale(scale, sideVec);
    mVelocity.set(scaledSideVec);

    if (isModelLongSide()) {
        mRotation.x += getNerveStep() % 12 < 6 ? 1.0f : -1.0f;
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        mVelocity.scale(0.5f, -mGravity);
    }

    MR::startLevelSound(this, "SE_OJ_LV_SPR_FLOATER_SIGN");

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(SpringWaterFloaterSpot, SpringWaterFloaterSpotNrvFloatUp));
    }
}

void SpringWaterFloaterSpot::exeFloatUp() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_SPR_FLOATER_FLOAT_ST");
    }

    MR::startLevelSound(this, "SE_OJ_LV_SPR_FLOATER_FLOAT");

    if (isNeedsForBound()) {
        mBoundStep = 0;
        mNeedsBound = true;
    }

    if (MR::isNearZero(mVelocity) && !mNeedsBound) {
        setNerve(GET_NERVE(SpringWaterFloaterSpot, SpringWaterFloaterSpotNrvFloatEnd));
    }
}

void SpringWaterFloaterSpot::exeFloatEnd() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();

        MR::validateClipping(this);

        MR::startSound(this, "SE_OJ_SPR_FLOATER_FLOAT_ED");

        mFloatTimer = MR::getRandom(60, 120);
    }

    mFloatTimer--;

    if (mFloatTimer <= 0) {
        MR::startSound(this, "SE_OJ_SPR_FLOATER_RIPPLE");
        mFloatTimer = MR::getRandom(60, 120);
    }

    if (isNeedsForBound()) {
        mBoundStep = 0;
        mNeedsBound = true;
    }
}

void SpringWaterFloaterSpot::startFloat() {
    if (isNerve(GET_NERVE(SpringWaterFloaterSpot, SpringWaterFloaterSpotNrvWait))) {
        MR::invalidateClipping(this);
    }
}

bool SpringWaterFloaterSpot::boundFloater() {
    if (!mNeedsBound) {
        return false;
    }

    if (mBoundStep == 0) {
        MR::startSound(this, "SE_OJ_SPR_FLOATER_BOUND");
    }

    if (mBoundStep == NebulaFrameClock::stepsOfMilliseconds(2500)) {
        mNeedsBound = false;
        mBoundSize = 0.0f;
        mBoundStep = 0;
        return true;
    }

    f32 scale = 20.0f - mBoundStep * (2.0f / 15.0f);
    mBoundSize = scale * MR::sinDegree(9.6f * static_cast< f32 >(mBoundStep));
    mBoundStep++;

    return false;
}

void SpringWaterFloaterSpot::floatFloater() {
    Vec3f up;
    up.negate(mGravity);

    Vec3f scaledUp;
    scaledUp.scale(mWaterCheckOffset, up);

    if (MR::isInWater(this, scaledUp)) {
        if (mHeight >= 90.0f) {
            mVelocity.scale(1.0f + mRiseSpeed, up);

            mHeight = 90.0f;

            if (mRiseSpeed < 40.0f) {
                mRiseSpeed += mRiseAccel;
            }

            return;
        }

        mHeight += 1.0f;

        return;
    }

    if (!mHasSurfaced) {
        MR::emitEffect(this, "Ripple");
        MR::setEffectHostMtx(this, "Ripple", mFloatMtx);

        mBoundStep = 0;
        mNeedsBound = true;
    }

    mHasSurfaced = true;
    mRiseSpeed = 0.0f;

    mRotation.set(mInitialRotation);
    mVelocity.zero();
}

bool SpringWaterFloaterSpot::isModelLongSide() {
    Joint* joint = MR::getJoint(this, static_cast< u16 >(0));
    return (joint->getMax()->x + joint->getMax()->z) / 2.0f > joint->getMax()->y;
}

bool SpringWaterFloaterSpot::isNeedsForBound() {
    if (mIsOnPlayer && MR::isPlayerJumpRising() && MR::checkPlayerActionTrigger()) {
        return true;
    }

    if (!mIsOnPlayer && MR::isOnPlayer(getSensor(nullptr))) {
        return true;
    }

    return false;
}
