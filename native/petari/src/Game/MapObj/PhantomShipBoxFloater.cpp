#include "Game/MapObj/PhantomShipBoxFloater.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FloaterFloatingForceTypeSpring.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const cEffectName = "Ripple";
};  // namespace

namespace NrvPhantomShipBoxFloater {
    NEW_NERVE(HostTypeWait, PhantomShipBoxFloater, Wait);
    NEW_NERVE(HostTypeMove, PhantomShipBoxFloater, Move);
    NEW_NERVE(HostTypeMoveUnderWater, PhantomShipBoxFloater, MoveUnderWater);
};  // namespace NrvPhantomShipBoxFloater

PhantomShipBoxFloater::PhantomShipBoxFloater(const char* pName)
    : LiveActor(pName), mObjectName(), mFloaterOffset(gZeroVec), mRippleSeCounter(), mSinkDepthUnder(0.0f), mSinkDepthOver(0.0f) {
    lumacore::setClass(this, lumacore::kClassId< PhantomShipBoxFloater >);
}

inline bool floaterIsMoving(FloaterFloatingForceTypeSpring* force) {
    return 0.1f < force->getCurrentVelocity().length();
}

void PhantomShipBoxFloater::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mFloaterOffset.set(mPosition);
    MR::getObjectName(&mObjectName, rIter);
    if (MR::isEqualString("PhantomShipBoxFloaterA", mObjectName)) {
        mSinkDepthUnder = 580.0f;
        mSinkDepthOver = 575.0f;
    } else if (MR::isEqualString("PhantomShipBoxFloaterB", mObjectName)) {
        mSinkDepthUnder = 380.0f;
        mSinkDepthOver = 375.0f;
    } else if (MR::isEqualString("PhantomShipBoxFloaterC", mObjectName)) {
        mSinkDepthUnder = 180.0f;
        mSinkDepthOver = 175.0f;
    }

    initModelManagerWithAnm(mObjectName, nullptr, false);
    MR::connectToSceneMapObj(this);
    initEffectKeeper(0, nullptr);
    MR::setEffectHostSRT(this, ::cEffectName, &mFloaterOffset, nullptr, nullptr);
    initSound(4, false);
    mRippleSeCounter = MR::getRandom(20, 90);
    initHitSensor(1);
    MR::initCollisionParts(this, mObjectName, MR::addBodyMessageSensorMapObj(this), nullptr);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    initNerve(GET_NERVE(PhantomShipBoxFloater, HostTypeWait));

    mFloatingForce = new FloaterFloatingForceTypeSpring(this, mObjectName);
    mFloatingForce->init(rIter);
    mFloatingForce->start();
    makeActorAppeared();
}

void PhantomShipBoxFloater::control() {
    mFloatingForce->movement();
    mFloatingForce->updateHostVelocity(&mVelocity);
}

f32 PhantomShipBoxFloater::getCurrentSinkDepth() const {
    Vec3f zDir, yDir;
    Pos3f depthMtx;
    depthMtx.setInline(getBaseMtx());
    depthMtx.getTrans(zDir);
    depthMtx.getYDir(yDir);
    f32 sign = MR::sign(yDir.dot(mFloaterOffset - zDir));
    return sign * mFloaterOffset.distance(zDir);
}

void PhantomShipBoxFloater::exeWait() {
    if (floaterIsMoving(mFloatingForce)) {
        setNerve(GET_NERVE(PhantomShipBoxFloater, HostTypeMove));
    }
}

void PhantomShipBoxFloater::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_BOX_FLOATER_BOUND");
        MR::emitEffect(this, ::cEffectName);
    }

    playRippleSe();

    if (MR::isOnPlayer(this)) {
        if (mSinkDepthUnder < getCurrentSinkDepth()) {
            MR::deleteEffect(this, ::cEffectName);
            setNerve(GET_NERVE(PhantomShipBoxFloater, HostTypeMoveUnderWater));
        }
    } else {
        if (!floaterIsMoving(mFloatingForce)) {
            MR::deleteEffect(this, ::cEffectName);
            setNerve(GET_NERVE(PhantomShipBoxFloater, HostTypeWait));
        }
    }
}

void PhantomShipBoxFloater::exeMoveUnderWater() {
    if (!MR::isOnPlayer(this)) {
        if (getCurrentSinkDepth() < mSinkDepthOver) {
            setNerve(GET_NERVE(PhantomShipBoxFloater, HostTypeMove));
        }
    }
}

void PhantomShipBoxFloater::playRippleSe() {
    if (--mRippleSeCounter <= 0) {
        MR::startSound(this, "SE_OJ_BOX_FLOATER_RIPPLE");
        mRippleSeCounter = MR::getRandom(20, 90);
    }
}
