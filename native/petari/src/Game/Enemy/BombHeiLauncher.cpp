#include "Game/Enemy/BombHeiLauncher.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 30.0f);

namespace {
    NEBULA_TUNABLE(f32, hThrowVel, 35.0f);
    NEBULA_TUNABLE(f32, hThrowFrontVel, 1.0f);
    static const s32 hCountTimer = NebulaFrameClock::stepsOfSeconds(1);
};  // namespace

namespace NrvBombHeiLauncher {
    NEW_NERVE(HostTypeNrvLauncherWait, BombHeiLauncher, LauncherWait);
    NEW_NERVE(HostTypeNrvStop, BombHeiLauncher, Stop);
};  // namespace NrvBombHeiLauncher

BombHeiLauncher::BombHeiLauncher(const char* pName) : LiveActor(pName), mBomb(), mCountdown(), mConnector(), mIsActive(true) {
    lumacore::setClass(this, lumacore::kClassId< BombHeiLauncher >);
    mConnector = new MapObjConnector(this);
}

void BombHeiLauncher::init(const TableIter& rIter) {
    bool isBombHeiLauncher = true;
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);

        const char* objectName = nullptr;
        MR::getObjectName(&objectName, rIter);
        if (MR::isEqualString(objectName, "BombLauncher")) {
            isBombHeiLauncher = false;
        }

        if (MR::useStageSwitchReadA(this, rIter)) {
            mIsActive = false;
            MR::listenStageSwitchOnOffA(this, MR::Functor(this, &BombHeiLauncher::onSwitchA), MR::Functor(this, &BombHeiLauncher::offSwitchA));
        }

        MR::useStageSwitchSleep(this, rIter);
    }

    initModelManagerWithAnm("BombHeiLauncher", nullptr, false);
    MR::connectToSceneCollisionEnemy(this);
    MR::initLightCtrl(this);
    MR::onCalcGravity(this);

    initSound(6, false);
    initEffectKeeper(0, nullptr);

    initHitSensor(2);
    MR::addBodyMessageSensorEnemy(this);
    MR::initCollisionParts(this, "BombHeiLauncher", getSensor("body"), nullptr);
    MR::onCalcGravity(this);

    initNerve(GET_NERVE(BombHeiLauncher, HostTypeNrvStop));
    mCountdown = -1;

    MR::validateClipping(this);
    MR::setGroupClipping(this, rIter, 32);
    makeActorDead();

    if (isBombHeiLauncher) {
        mBomb = new BombHei("ボム兵");
        mBomb->mType = BombHei::BOMB_HEI;
    } else {
        mBomb = new BombHei("ボム");
        mBomb->mType = BombHei::BOMB;
    }

    mBomb->mPosition.set(mPosition);
    mBomb->initWithoutIter();
    mBomb->setGroupClipping(rIter);

    s32 ignoreCollisionArg = -1;
    MR::getPlacementArg2NoInit(rIter, &ignoreCollisionArg);
    if (ignoreCollisionArg != -1) {
        mBomb->mExplosionIgnoreCollision = true;
    }

    mBomb->kill();
}

void BombHeiLauncher::initAfterPlacement() {
    Vec3f up;
    MR::calcUpVec(&up, this);
    MR::resetPosition(mBomb, mPosition + up * cNum_initAfterPlacement_f1);
    mBomb->mGravity.set(-up);
    mBomb->initAfterPlacement();
    mConnector->attachToUnder();
    makeActorAppeared();
}

void BombHeiLauncher::startCountdown() {
    if (mCountdown != -1) {
        return;
    }

    mCountdown = ::hCountTimer;
}

void BombHeiLauncher::onSwitchA() {
    mIsActive = true;
    mCountdown = 0;
    setNerve(GET_NERVE(BombHeiLauncher, HostTypeNrvLauncherWait));
}

void BombHeiLauncher::offSwitchA() {
    mIsActive = false;
    mCountdown = -1;
    setNerve(GET_NERVE(BombHeiLauncher, HostTypeNrvStop));

    if (mBomb != nullptr && !MR::isDead(mBomb)) {
        mBomb->kill();
    }
}

void BombHeiLauncher::exeLauncherWait() {
    if (!mIsActive) {
        return;
    }

    if (mCountdown == 0) {
        if (MR::isOnPlayerShadow(this) || MR::isOnPlayer(this)) {
            return;
        }

        MR::startBck(this, "Open");
        MR::startSound(this, "SE_EM_BOMBLAUN_LAUNCH");

        mBomb->appear();
        MR::resetPosition(mBomb, mPosition);

        Vec3f up, front;
        MR::calcUpVec(&up, this);
        MR::calcFrontVec(&front, this);
        getBombVelocity()->set(up * ::hThrowVel);
        getBombVelocity()->add(front * ::hThrowFrontVel);
        setNerve(GET_NERVE(BombHeiLauncher, HostTypeNrvStop));
        return;
    }

    if (MR::isDead(mBomb)) {
        startCountdown();
    }
}

void BombHeiLauncher::exeStop() {
    if (MR::isFirstStep(this)) {
        mCountdown = -1;
    }

    if (mIsActive && MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(BombHeiLauncher, HostTypeNrvLauncherWait));
    }
}

void BombHeiLauncher::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mConnector->connect();
}

void BombHeiLauncher::control() {
    if (mCountdown <= 0) {
        return;
    }

    mCountdown--;
}
