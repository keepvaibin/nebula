#include "Game/Map/KoopaBattleMapPlate.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f2, "receiveOtherMsg.f2", 50.0f);

KoopaBattleMapPlate::KoopaBattleMapPlate(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< KoopaBattleMapPlate >);
    mBaseMtx.identity();
}

void KoopaBattleMapPlate::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
}

void KoopaBattleMapPlate::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::calcGravity(this);
    MR::makeMtxUpNoSupportPos(&mBaseMtx, -mGravity, mPosition);
    initModelManagerWithAnm("KoopaPlate", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensor(this, "Attack", ATYPE_KOOPA_PLATE, 8, cNum_init_f1, Vec3f(0.0f, -cNum_init_f2, 0.0f));
    MR::initCollisionParts(this, "KoopaPlate", getSensor("Attack"), mBaseMtx);
    initEffectKeeper(1, nullptr);
    MR::addEffectHitNormal(this, "Hit");
    MR::setEffectBaseScale(this, "Hit", cNum_init_f3);
    initSound(4, false);
    MR::setClippingFarMax(this);
    MR::declareStarPiece(this, 5);
    makeActorAppeared();
}

bool KoopaBattleMapPlate::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg != ACTMES_KOOPA_HIP_DROP_ATTACK) {
        return false;
    }

    MR::emitEffectHit(this, MR::getSensorHost(pSender)->mPosition, "Hit");
    MR::startSound(this, "SE_OJ_KOOPA_PLATE_PLN_BRK");
    MR::appearStarPiece(this, mPosition, 5, cNum_receiveOtherMsg_f1, cNum_receiveOtherMsg_f2, false);
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_F");
    MR::emitEffect(this, "Break");
    kill();

    return true;
}
