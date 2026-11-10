#include "Game/Boss/TripodBossFixPartsBase.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Boss/TripodBossBreakMovement.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initClippingSphere_f1, "initClippingSphere.f1", 0.5f);

TripodBossFixPartsBase::TripodBossFixPartsBase(const char* pName)
    : LiveActor(pName), mLocalPos(0.0f, 0.0f, 0.0f), mBreakMovement(), mJointID(-1), mClipCenter(0.0f, 0.0f, 0.0f), mClippingRadius(), mIsActive() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossFixPartsBase >);
    mMtx.identity();
}

void TripodBossFixPartsBase::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mLocalPos = mPosition;
    MR::getPlacementArg0NoInit(rIter, &mJointID);
    MR::addTripodBossParts(this);
    MR::invalidateClipping(this);
}

void TripodBossFixPartsBase::initAfterPlacement() {
    updateTripodMatrix();
    calcAnim();
    MR::offCalcAnim(this);
}

void TripodBossFixPartsBase::initClippingSphere() {
    Box3f boundingBox;
    MR::calcModelBoundingBox(&boundingBox, this);

    mClipCenter = (boundingBox.i + boundingBox.f) * cNum_initClippingSphere_f1;
    mClippingRadius = mClipCenter.distance(boundingBox.i);
}

void TripodBossFixPartsBase::setClippingSphere(const Vec3f& rCenter, f32 radius) {
    mClipCenter = rCenter;
    mClippingRadius = radius;
}

void TripodBossFixPartsBase::makeActorDead() {
    LiveActor::makeActorDead();

    if (mBreakMovement != nullptr) {
        mBreakMovement->makeActorDead();
    }
}

void TripodBossFixPartsBase::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mMtx);
}

bool TripodBossFixPartsBase::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_TRIPODBOSS_STARTED) {
        activateTripodBoss();

        return true;
    }

    return false;
}

void TripodBossFixPartsBase::calcTripodLocalMatrix(Pos3f* pMtx) {
    MR::makeMtxTR(*pMtx, mLocalPos, mRotation);
}

void TripodBossFixPartsBase::activateTripodBoss() {
}

void TripodBossFixPartsBase::startBreakMovement() {
    Pos3f mtx;
    mtx.set(getBaseMtx());

    mBreakMovement->start(mtx, mJointID);
}

void TripodBossFixPartsBase::updateBreakMovementMatrix() {
    mBreakMovement->movement();

    mMtx = mBreakMovement->mMtx;
    mMtx.getTrans(mPosition);
}

void TripodBossFixPartsBase::updateTripodMatrix() {
    calcTripodLocalMatrix(&mMtx);
    MR::concatTripodBossAttachJointMatrix(&mMtx, mJointID);
    mMtx.getTrans(mPosition);

    if (!mIsActive) {
        return;
    }

    Vec3f mul;
    mMtx.mult(mClipCenter, mul);

    if (MR::isJudgedToClipFrustum(mul, mClippingRadius)) {
        MR::hideModelAndOnCalcAnim(this);
    } else {
        MR::showModel(this);
    }
}

void TripodBossFixPartsBase::initBreakMovement(s32 level) {
    mBreakMovement = new TripodBossBreakMovement("三脚ボスパーツ破壊挙動");
    mBreakMovement->setBreakDownLevel(level);
}
