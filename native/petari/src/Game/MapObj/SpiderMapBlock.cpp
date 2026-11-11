#include "Game/MapObj/SpiderMapBlock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sCoinSpeed, 35.0f);
};  // namespace

SpiderMapBlock::SpiderMapBlock(const char* pName) : LiveActor(pName), mPosZ() {
    lumacore::setClass(this, lumacore::kClassId< SpiderMapBlock >);
}

bool SpiderMapBlock::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgPlayerHipDropFloor(msg)) {
        kill();
        return true;
    }
    return false;
}

bool SpiderMapBlock::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    kill();
    return true;
}

void SpiderMapBlock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mPosZ = mPosition.z;
    initModelManagerWithAnm("SpiderMapBlock", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, "SpiderMapBlock", getSensor("body"), nullptr);
    initEffectKeeper(8, nullptr);
    initSound(4, false);
    MR::declareCoin(this, 1);
    makeActorAppeared();
}

void SpiderMapBlock::kill() {
    MR::startSound(this, "SE_OJ_ROCK_BREAK");
    MR::emitEffect(this, "Break");
    MtxPtr baseMtx = getBaseMtx();
    Vec3f coinVel(baseMtx[0][1], baseMtx[1][1], 0.0f);
    MR::normalizeOrZero(&coinVel);
    coinVel *= ::sCoinSpeed;
    Vec3f coinPos(mPosition.x, mPosition.y, mPosZ);
    MR::appearCoinToVelocity(this, coinPos, coinVel, 1);
    LiveActor::kill();
}
