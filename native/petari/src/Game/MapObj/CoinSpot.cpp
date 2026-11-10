#include "Game/MapObj/CoinSpot.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 60.0f);

namespace NrvCoinSpot {
    NEW_NERVE(CoinSpotNrvWait, CoinSpot, Wait);
    NEW_NERVE(CoinSpotNrvIsInTornado, CoinSpot, IsInTornado);
};  // namespace NrvCoinSpot

CoinSpot::CoinSpot(const char* pName) : LiveActor(pName), mCoinCount(8), mIsInTornado(), mSpawnTimer(4), mSpawnedCount() {
}

void CoinSpot::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovementCalcAnim(this);

    MR::initDefaultPos(this, rIter);

    s32 arg = 0;
    MR::getPlacementArg0NoInit(rIter, &arg);

    if (arg > 0) {
        mCoinCount = arg;
    } else {
        mCoinCount = 8;
    }

    initSound(4, false);
    initSensor();

    initNerve(GET_NERVE(CoinSpot, CoinSpotNrvWait));

    appear();
}

void CoinSpot::initSensor() {
    f32 scale = mScale.y;
    initHitSensor(1);
    Vec3f offset(0.0f, cNum_initSensor_f1 * scale, 0.0f);
    MR::addHitSensorMapObj(this, "body", 32, offset.y, offset);
}

void CoinSpot::exeWait() {
    if (MR::isFirstStep(this)) {
        mSpawnTimer = 0;
        mSpawnedCount = 0;
        mIsInTornado = false;
    }

    if (mIsInTornado) {
        setNerve(GET_NERVE(CoinSpot, CoinSpotNrvIsInTornado));
    }
}

void CoinSpot::exeIsInTornado() {
    if (MR::isFirstStep(this)) {
        mSpawnTimer = 0;
        mSpawnedCount = 0;
    }

    if (mIsInTornado) {
        if (--mSpawnTimer <= 0) {
            MR::appearCoinFix(this, mPosition, 1);
            mSpawnTimer = 4;
            mSpawnedCount++;
        }

        mIsInTornado = false;
    } else {
        mSpawnTimer = 0;
    }

    if (mSpawnedCount >= mCoinCount || MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        MR::deleteEffect(this, "光");
        kill();
    }
}

void CoinSpot::attackSensor(HitSensor*, HitSensor*) {
}
