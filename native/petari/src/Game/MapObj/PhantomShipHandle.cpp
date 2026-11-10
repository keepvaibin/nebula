#include "Game/MapObj/PhantomShipHandle.hpp"
#include "Game/MapObj/MapPartsSeesaw1AxisRotator.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.1f);

namespace {
    NEBULA_TUNABLE(f32, sClippingRadius, 200.0f);
};  // namespace

PhantomShipHandle::PhantomShipHandle(const char* pName) : LiveActor(pName), mRotator() {
}

void PhantomShipHandle::control() {
    mRotator->movement();
}

void PhantomShipHandle::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    baseMtx.set(mRotator->getRotateMtx());
    baseMtx.setTrans(mPosition);

    MR::setBaseTRMtx(this, baseMtx);
}

void PhantomShipHandle::init(const TableIter& rIter) {
    MR::initDefaultPosNoRepeat(this, rIter);
    initModelManagerWithAnm("PhantomShipHandle", nullptr, false);

    MR::connectToSceneEnvironment(this);

    initHitSensor(1);
    MR::initCollisionParts(this, "PhantomShipHandle", MR::addBodyMessageSensorMapObj(this), nullptr);

    MR::setClippingTypeSphere(this, ::sClippingRadius);
    MR::setClippingFar300m(this);

    initSound(4, false);

    mRotator = new MapPartsSeesaw1AxisRotator(this, "SE_OJ_LV_P_SHIP_HANDLE_MOVE", cNum_init_f1);
    mRotator->init(rIter);
    mRotator->start();

    makeActorAppeared();
}
