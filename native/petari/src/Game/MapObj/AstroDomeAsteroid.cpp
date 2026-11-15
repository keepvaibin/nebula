#include "Game/MapObj/AstroDomeAsteroid.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/SphereSelector.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cBckRate = 1.0f;
};  // namespace

namespace NrvAstroDomeAsteroid {
    NEW_NERVE(AstroDomeAsteroidNrvWait, AstroDomeAsteroid, Wait);
};  // namespace NrvAstroDomeAsteroid

AstroDomeAsteroid::AstroDomeAsteroid(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< AstroDomeAsteroid >);
    mBaseMtx.identity();
}

void AstroDomeAsteroid::init(const TableIter& rIter) {
    initModelManagerWithAnm("AstroDomeAsteroid", nullptr, false);
    MR::connectToSceneMapObj(this);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(AstroDomeAsteroid, AstroDomeAsteroidNrvWait));
    MR::tryRegisterDemoCast(this, rIter);
    MR::registerDemoSimpleCastAll(this);
    SphereSelectorFunction::registerTarget(this);
    makeActorDead();
}

void AstroDomeAsteroid::appear() {
    LiveActor::appear();
    mBaseMtx.identity();
}

void AstroDomeAsteroid::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "AstroDomeAsteroid");
    }

    MR::setBckRate(this, ::cBckRate);
}

void AstroDomeAsteroid::control() {
    SphereSelectorFunction::calcHandledTrans(Vec3f(0.0f, 0.0f, 0.0f), &mPosition);
    SphereSelectorFunction::calcHandledRotateMtx(Vec3f(0.0f, 0.0f, 0.0f), &mBaseMtx);
}

void AstroDomeAsteroid::calcAndSetBaseMtx() {
    Pos3f mtx(mBaseMtx);
    mtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, mtx);
}

bool AstroDomeAsteroid::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return SphereSelectorFunction::trySyncAppearMsgSelectStart(this, msg);
}
