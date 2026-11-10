#include "Game/MapObj/SimpleFloaterObj.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsFloatingForce.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);

namespace {
    const char* cEffectName = "Ripple";
};  // namespace

namespace NrvSimpleFloaterObj {
    NEW_NERVE(SimpleFloaterObjNrvWait, SimpleFloaterObj, Wait);
};  // namespace NrvSimpleFloaterObj

SimpleFloaterObj::SimpleFloaterObj(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SimpleFloaterObj >);
    mObjName = 0;
    mFloatingForce = nullptr;
    mEffectPos.set(0.0f, 0.0f, 0.0f);
}

void SimpleFloaterObj::init(const TableIter& rrIter) {
    MR::initDefaultPos(this, rrIter);
    MR::getObjectName(&mObjName, rrIter);
    initModelManagerWithAnm(mObjName, nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    HitSensor* mSensor = getSensor(nullptr);
    MR::initCollisionParts(this, mObjName, mSensor, nullptr);
    initEffectKeeper(0, nullptr);
    mEffectPos.set(mPosition);
    MR::setEffectHostSRT(this, ::cEffectName, &mEffectPos, &mRotation, nullptr);
    initSound(4, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f1);
    mFloatingForce = new MapPartsFloatingForce(this);
    mFloatingForce->init(rrIter);
    mFloatingForce->setObjectName(mObjName);
    mFloatingForce->start();
    initNerve(GET_NERVE(SimpleFloaterObj, SimpleFloaterObjNrvWait));
    makeActorAppeared();
}

void SimpleFloaterObj::exeWait() {

    if (!isVelocityClose() && !MR::isEffectValid(this, ::cEffectName)) {
        MR::emitEffect(this, ::cEffectName);
    } else if (isVelocityClose() && MR::isEffectValid(this, ::cEffectName)) {
        MR::deleteEffect(this, ::cEffectName);
    }

    mFloatingForce->movement();
}
