#include "Game/Map/KoopaBattleMapCoinPlate.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 2.5f);

namespace {
    NEBULA_TUNABLE(f32, sSideSpeed, 30.0f);
    NEBULA_TUNABLE(f32, sHopSpeed, 30.0f);
};  // namespace

KoopaBattleMapCoinPlate::KoopaBattleMapCoinPlate(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< KoopaBattleMapCoinPlate >);
    mBaseMtx.identity();
}

void KoopaBattleMapCoinPlate::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
}

void KoopaBattleMapCoinPlate::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::calcGravity(this);

    MR::makeMtxUpNoSupportPos(&mBaseMtx, -mGravity, mPosition);

    initModelManagerWithAnm("KoopaPlateCoin", nullptr, false);

    MR::connectToSceneMapObj(this);

    initHitSensor(1);
    MR::initCollisionParts(this, "KoopaPlateCoin", MR::addHitSensor(this, "Attack", ATYPE_KOOPA_COIN_PLATE, 8, cNum_init_f1, Vec3f(0.0f, -cNum_init_f2, 0.0f)),
                           mBaseMtx);
    initEffectKeeper(1, nullptr);

    MR::addEffectHitNormal(this, "Hit");
    MR::setEffectBaseScale(this, "Hit", cNum_init_f3);

    initSound(4, false);

    MR::setClippingFarMax(this);

    MR::declareCoin(this, 1);

    makeActorAppeared();
}

bool KoopaBattleMapCoinPlate::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg != ACTMES_KOOPA_HIP_DROP_ATTACK) {
        return false;
    }

    MR::emitEffectHit(this, MR::getSensorHost(pSender)->mPosition, "Hit");

    MR::startSound(this, "SE_OJ_KOOPA_PLATE_COIN_BRK");

    MR::calcGravity(this);

    Vec3f randomVec;
    MR::calcRandomVec(&randomVec, -::sSideSpeed, ::sSideSpeed);
    MR::vecKillElement(randomVec, mGravity, &randomVec);

    randomVec.add(mGravity * -::sHopSpeed);
    MR::appearCoinToVelocity(this, mPosition, randomVec, 1);

    MR::emitEffect(this, "Break");

    kill();

    return true;
}
