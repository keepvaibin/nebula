#include "Game/Boss/TripodBossKillerGenerater.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Boss/TripodBossFixPartsBase.hpp"
#include "Game/Enemy/HomingKiller.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const s32 sDemoAnimStartDelayTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    static const s32 sStopTime = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(f32, sCanRestartPlayerDistance, 230.0f);
};  // namespace

namespace NrvTripodBossKillerGenerater {
    NEW_NERVE(TripodBossKillerGeneraterNrvNonActive, TripodBossKillerGenerater, NonActive);
    NEW_NERVE(TripodBossKillerGeneraterNrvHide, TripodBossKillerGenerater, Hide);
    NEW_NERVE(TripodBossKillerGeneraterNrvShowDemo, TripodBossKillerGenerater, ShowDemo);
    NEW_NERVE(TripodBossKillerGeneraterNrvReady, TripodBossKillerGenerater, Ready);
    NEW_NERVE(TripodBossKillerGeneraterNrvShootSetting, TripodBossKillerGenerater, ShootSetting);
    NEW_NERVE(TripodBossKillerGeneraterNrvShoot, TripodBossKillerGenerater, Shoot);
    NEW_NERVE(TripodBossKillerGeneraterNrvCoolDown, TripodBossKillerGenerater, CoolDown);
    NEW_NERVE(TripodBossKillerGeneraterNrvBreak, TripodBossKillerGenerater, Break);
    NEW_NERVE(TripodBossKillerGeneraterNrvStop, TripodBossKillerGenerater, Stop);
    NEW_NERVE(TripodBossKillerGeneraterNrvRestart, TripodBossKillerGenerater, Restart);
};  // namespace NrvTripodBossKillerGenerater

TripodBossKillerGenerater::TripodBossKillerGenerater(const char* pName)
    : TripodBossFixPartsBase(pName), mKiller(), mCanShootSettingPlayerDistance(2000.0f), mActiveLabel(), mHasCollision(true), mIsWaitPlayerAway() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossKillerGenerater >);
    mLocalMtx.identity();
}

void TripodBossKillerGenerater::init(const TableIter& rIter) {
    TripodBossFixPartsBase::init(rIter);
    MR::makeMtxTR(mLocalMtx, this);
    initModelManagerWithAnm("TripodBossKillerCannon", nullptr, false);
    MR::connectToScene(this, MR::MovementType_MapObjDecoration, MR::CalcAnimType_MapObjDecoration, MR::DrawBufferType_TripodBoss, MR::DrawType_None);
    MR::getPlacementArg1NoInit(rIter, &mCanShootSettingPlayerDistance);
    initClippingSphere();

    mKiller = new HomingKiller("ホーミングキラー");
    mKiller->initWithoutIter();

    s32 chase;
    MR::getPlacementArg2WithInit(rIter, &chase);

    if (chase == 0) {
        mKiller->mDisableChase = true;
    }

    if (mHasCollision) {
        initHitSensor(1);
        MR::addHitSensor(this, "body", ATYPE_TRIPODBOSS_KILLER_GENERATER, 0, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
        MR::initCollisionParts(this, "TripodBossKillerCannon", getSensor("body"), nullptr);
    }

    initEffectKeeper(0, "TripodBoss");
    initSound(4, false);
    initNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvNonActive));
    MR::useStageSwitchReadAppear(this, rIter);

    if (mActiveLabel > 0) {
        MR::startBck(this, "2ndDemo");
        MR::setBckFrameAndStop(this, 0.0f);
    } else {
        MR::startBck(this, "Shoot");
        MR::setBckFrameAndStop(this, 0.0f);
    }

    if (mHasCollision) {
        MR::invalidateCollisionParts(this);
    }

    makeActorAppeared();
}

void TripodBossKillerGenerater::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "ExplosionM");
}

bool TripodBossKillerGenerater::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgExplosionAttack(msg)) {
        return requestBreak();
    }

    return false;
}

void TripodBossKillerGenerater::calcTripodLocalMatrix(Pos3f* pMtx) {
    pMtx->set(mLocalMtx);
}

void TripodBossKillerGenerater::activateTripodBoss() {
    MR::onCalcAnim(this);

    if (mActiveLabel > 0) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvHide));

        if (mHasCollision) {
            MR::invalidateCollisionParts(this);
        }
    } else {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvReady));

        if (mHasCollision) {
            MR::validateCollisionParts(this);
        }
    }

    mIsActive = true;
}

void TripodBossKillerGenerater::setLocalMatrix(const Pos3f& rPMtx) {
    mLocalMtx.set(rPMtx);
}

void TripodBossKillerGenerater::setActiveLebel(s32 label) {
    mActiveLabel = label;
}

void TripodBossKillerGenerater::setHasCollision(bool hasCollision) {
    mHasCollision = hasCollision;
}

bool TripodBossKillerGenerater::tryShootSetting() {
    if (MR::isValidSwitchAppear(this) && !MR::isOnSwitchAppear(this)) {
        return false;
    }

    if (MR::isSteppingTripodBossJointID(mJointID)) {
        return false;
    }

    Vec3f trans;
    mMtx.getTrans(trans);

    if (MR::isDead(mKiller)) {
        if (MR::getPlayerPos()->distance(trans) < mCanShootSettingPlayerDistance) {
            Vec3f front;
            MR::calcFrontVec(&front, this);

            Vec3f trans;
            mMtx.getTrans(trans);

            mKiller->appear(trans, front);

            setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvShootSetting));

            return true;
        }
    }

    return false;
}

bool TripodBossKillerGenerater::tryShoot() {
    if (mKiller->isMoveStart()) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvShoot));

        return true;
    }

    return false;
}

bool TripodBossKillerGenerater::tryCoolDown() {
    if (MR::isDead(mKiller)) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvCoolDown));

        return true;
    }

    return false;
}

bool TripodBossKillerGenerater::tryReady() {
    if (MR::isGreaterStep(this, 0)) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvReady));

        return true;
    }

    return false;
}

bool TripodBossKillerGenerater::tryBreak() {
    if (MR::isEndBreakDownDemoTripodBoss()) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvBreak));

        return true;
    }

    return false;
}

bool TripodBossKillerGenerater::tryAbort() {
    if (!MR::isStartedTripodBoss() || MR::isDemoTripodBoss()) {
        if (!MR::isDead(mKiller)) {
            mKiller->kill();
        }

        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvReady));

        return true;
    }

    return false;
}

bool TripodBossKillerGenerater::requestBreak() {
    if (isNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvReady)) ||
        isNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvShootSetting)) ||
        isNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvShoot)) ||
        isNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvCoolDown))) {
        if (isNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvShootSetting)) && !MR::isDead(mKiller)) {
            mKiller->kill();
        }

        if (mHasCollision) {
            MR::invalidateCollisionParts(this);
        }

        MR::startBck(this, "2ndDemo");
        MR::setBckFrameAndStop(this, 0.0f);
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvStop));

        return true;
    }

    return false;
}

void TripodBossKillerGenerater::exeNonActive() {
}

void TripodBossKillerGenerater::exeHide() {
    if (MR::isDamageDemoTripodBoss()) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvShowDemo));
    }

    updateTripodMatrix();
}

void TripodBossKillerGenerater::exeShowDemo() {
    if (MR::isStep(this, ::sDemoAnimStartDelayTime)) {
        if (MR::getPlayerPos()->distance(mPosition) < ::sCanRestartPlayerDistance) {
            setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvRestart));
            return;
        }

        MR::startBck(this, "2ndDemo");

        if (mHasCollision) {
            MR::validateCollisionParts(this);
        }
    }

    updateTripodMatrix();


    if (!MR::isDamageDemoTripodBoss()) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvReady));
    }
}

void TripodBossKillerGenerater::exeReady() {
    updateTripodMatrix();

    if (!tryBreak() && !tryAbort() && !tryShootSetting()) {
        return;
    }
}

void TripodBossKillerGenerater::exeShootSetting() {
    updateTripodMatrix();

    Vec3f front;
    MR::calcFrontVec(&front, this);

    Vec3f trans;
    mMtx.getTrans(trans);

    HomingKiller* killer = mKiller;
    killer->mBasePos.set(trans);
    killer->mBaseFront.set(front);

    if (!tryBreak() && !tryAbort() && !tryShoot()) {
        return;
    }
}

void TripodBossKillerGenerater::exeShoot() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shoot");
        mPosition.distance(*MR::getPlayerPos());
    }

    updateTripodMatrix();

    if (!tryBreak() && !tryAbort() && !tryCoolDown()) {
        return;
    }
}

void TripodBossKillerGenerater::exeCoolDown() {
    updateTripodMatrix();

    if (!tryBreak() && !tryAbort() && !tryReady()) {
        return;
    }
}

void TripodBossKillerGenerater::exeStop() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "ExplosionS");
        MR::startSound(this, "SE_BM_TRIPOD_CANNON_BREAK");

        if (mHasCollision) {
            MR::invalidateCollisionParts(this);
        }
    }

    updateTripodMatrix();

    if (MR::isGreaterStep(this, ::sStopTime)) {
        setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvRestart));
    }
}

void TripodBossKillerGenerater::exeRestart() {
    if (MR::isFirstStep(this)) {
        mIsWaitPlayerAway = true;
    }

    if (mIsWaitPlayerAway) {
        if (MR::getPlayerPos()->distance(mPosition) >= ::sCanRestartPlayerDistance) {
            MR::startBck(this, "2ndDemo");
            MR::startSound(this, "SE_BM_TRIPOD_CANNON_RESTART");

            if (mHasCollision) {
                MR::validateCollisionParts(this);
            }

            mIsWaitPlayerAway = false;
        }
    }

    updateTripodMatrix();

    if (!mIsWaitPlayerAway) {
        if (MR::isBckStopped(this)) {
            setNerve(GET_NERVE(TripodBossKillerGenerater, TripodBossKillerGeneraterNrvReady));
        }
    }
}

void TripodBossKillerGenerater::exeBreak() {
    if (MR::isFirstStep(this)) {
        if (!MR::isDead(mKiller)) {
            mKiller->kill();
        }

        kill();
    }
}
