#include "Game/MapObj/TreasureSpot.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);

namespace {
    NEBULA_TUNABLE(s32, sNumCoin, 1);
    NEBULA_TUNABLE(f32, sEnableSpoutLength, 2000.0f);
    NEBULA_TUNABLE(f32, sGlowLeaveMargin, 200.0f);
};  // namespace

namespace NrvTreasureSpot {
    NEW_NERVE(TreasureSpotNrvWait, TreasureSpot, Wait);
    NEW_NERVE(TreasureSpotNrvEnd, TreasureSpot, End);
    NEW_NERVE(TreasureSpotNrvSpout, TreasureSpot, Spout);
};  // namespace NrvTreasureSpot

TreasureSpot::TreasureSpot(const char* pName) : MapObjActor(pName), mIsCoinFlower() {
}

void TreasureSpot::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupHitSensor();
    info.setupHitSensorParam(4, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    info.setupNerve(GET_NERVE(TreasureSpot, TreasureSpotNrvWait));
    info.setupSound(4);
    MapObjActor::initialize(rIter, info);
    mIsCoinFlower = isObjectName("CoinFlower");
    MR::initStarPointerTarget(this, cNum_init_f3, Vec3f(0.0f, cNum_init_f4, 0.0f));
    MR::declareCoin(this, ::sNumCoin);
}

void TreasureSpot::exeWait() {
}

void TreasureSpot::exeEnd() {
}

void TreasureSpot::exeSpout() {
    if (MR::isFirstStep(this)) {
        MR::forceDeleteEffectAll(this);
        Vec3f upVec;
        MR::calcUpVec(&upVec, this);
        MR::appearCoinPopToDirection(this, mPosition, upVec, ::sNumCoin);

        if (mIsCoinFlower) {
            MR::startBck(this, "Bloom");
            MR::startSound(this, "SE_OJ_COIN_FLOWER_BLOOM");
        } else {
            setNerve(GET_NERVE(TreasureSpot, TreasureSpotNrvEnd));
            kill();
            return;
        }
    }

    if (mIsCoinFlower) {
        if (MR::isBckStopped(this)) {
            setNerve(GET_NERVE(TreasureSpot, TreasureSpotNrvEnd));
        }
    }
}

bool TreasureSpot::isSpouted() const {
    return isNerve(GET_NERVE(TreasureSpot, TreasureSpotNrvSpout)) || isNerve(GET_NERVE(TreasureSpot, TreasureSpotNrvEnd));
}

void TreasureSpot::control() {
    if (!isSpouted()) {
        switchEmitGlow();
    }
}

bool TreasureSpot::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isSpouted() || !MR::isNearPlayerAnyTime(this, ::sEnableSpoutLength)) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(TreasureSpot, TreasureSpotNrvSpout));
        return true;
    }

    return false;
}

void TreasureSpot::switchEmitGlow() {
    // Glow starts inside the spout range and ends only a margin past it: one test for both deleted and made it again
    // every few steps for a player standing at the range.
    mIsGlowNear = MR::isNearPlayerAnyTime(this, mIsGlowNear ? ::sEnableSpoutLength + ::sGlowLeaveMargin : ::sEnableSpoutLength);
    if (!mIsGlowNear) {
        MR::deleteEffect(this, "Glow");
    } else if (!MR::isEffectValid(this, "Glow")) {
        MR::emitEffect(this, "Glow");
    }
}
