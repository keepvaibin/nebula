#include "Game/MapObj/RosettaChair.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);

RosettaChair::RosettaChair(const char* pName) : LiveActor(pName), mDefaultPosition(0.0f, 0.0f, 0.0f), mDefaultRotation(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< RosettaChair >);
    mScaleMtx.identity();
}

void RosettaChair::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("RosettaChair", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);

    mScaleMtx.set(getBaseMtx());
    mScaleMtx.scaleXYZ(cNum_init_f1);
    MR::initCollisionPartsAutoEqualScale(this, "RosettaChair", getSensor("body"), mScaleMtx);

    MR::setClippingTypeSphere(this, cNum_init_f2);
    MR::tryRegisterDemoCast(this, rIter);
    MR::registerDemoActionFunctor(this, MR::Functor(this, &RosettaChair::startDemo), "朗読開始");
    MR::registerDemoActionFunctor(this, MR::Functor(this, &RosettaChair::setDefaultPose), "キャスト入れ換え");
    mDefaultPosition.set(mPosition);
    mDefaultRotation.set(mRotation);
    MR::startBck(this, "RosettaChair");
    makeActorAppeared();
}

void RosettaChair::setDefaultPose() {
    mPosition.set(mDefaultPosition);
    mRotation.set(mDefaultRotation);
    MR::startBck(this, "RosettaChair");
    MR::validateCollisionParts(this);
}

void RosettaChair::startDemo() {
    MR::startBck(this, "DemoRosettaReading");
    MR::invalidateCollisionParts(this);
}
