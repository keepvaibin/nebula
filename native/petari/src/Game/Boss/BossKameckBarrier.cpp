#include "Game/Boss/BossKameckBarrier.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // Added to the barrier's height where it is placed (0).
    NEBULA_TUNABLE(f32, cOffsetY, 0.0f);
};  // namespace

namespace NrvBossKameckBarrier {
    NEW_NERVE(BossKameckBarrierNrvWait, BossKameckBarrier, Wait);
};  // namespace NrvBossKameckBarrier

BossKameckBarrier::BossKameckBarrier(const char* pName, const char* pModelName) : LiveActor(pName), mModelName(pModelName) {
    lumacore::setClass(this, lumacore::kClassId< BossKameckBarrier >);
}

void BossKameckBarrier::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    if (::cOffsetY != 0.0f) {
        mPosition.y += ::cOffsetY;
    }
    initModelManagerWithAnm(mModelName, nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_KAMECK_BARRIER, 0, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, mModelName, getSensor("body"), nullptr);
    initNerve(GET_NERVE(BossKameckBarrier, BossKameckBarrierNrvWait));
    MR::invalidateClipping(this);
    makeActorDead();
}

void BossKameckBarrier::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Wait");
        MR::startBtk(this, "Wait");
    }
}
