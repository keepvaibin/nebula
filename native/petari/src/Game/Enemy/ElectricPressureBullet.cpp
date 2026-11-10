#include "Game/Enemy/ElectricPressureBullet.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 75.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f1, "exeFly.f1", 4.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeFly_1, "exeFly.1", 10);

namespace NrvElectricPressureBullet {
    NEW_NERVE(ElectricPressureBulletNrvFly, ElectricPressureBullet, Fly);
};  // namespace NrvElectricPressureBullet

ElectricPressureBullet::ElectricPressureBullet(const char* pName) : LiveActor(pName), mFront(0.0f, 0.0f, 0.0f), mSpeed(0.0f) {
}

void ElectricPressureBullet::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("ElectricBullet", nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(1);
    MR::addHitSensorEnemyAttack(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(cNum_init_f2, 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initSound(1, false);
    MR::initStarPointerTarget(this, cNum_init_f3, Vec3f(0, 0, 0));
    MR::initShadowVolumeCylinder(this, cNum_init_f4);
    initNerve(GET_NERVE(ElectricPressureBullet, ElectricPressureBulletNrvFly));
    makeActorDead();
}

void ElectricPressureBullet::kill() {
    MR::emitEffect(this, "Break");
    MR::startSound(this, "SE_EM_ELECBUBLLET_BREAK");
    LiveActor::kill();
}

void ElectricPressureBullet::shotElectricBullet(LiveActor* /*actor*/, const Pos3f& pos, const f32& value) {
    mSpeed = value;
    pos.getZDir(mFront);
    mVelocity.scale(mSpeed, mFront);
    pos.getTrans(mPosition);
    mRotation.zero();
    makeActorAppeared();
    MR::validateHitSensors(this);
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(ElectricPressureBullet, ElectricPressureBulletNrvFly));
}

void ElectricPressureBullet::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shot");
        MR::startBtk(this, "ElectricBullet");
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startBck(this, "Move");
    }
    MR::startLevelSound(this, "SE_EM_LV_ELECBUBLLET_FLY");
    if (MR::isGreaterEqualStep(this, cStep_exeFly_1)) {
        if (MR::isLessStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
            if (getNerveStep() % 5 == 0) {
                Vec3f toPlayer;
                toPlayer.sub(*MR::getPlayerCenterPos(), mPosition);
                MR::normalize(&toPlayer);
                MR::turnVecToVecDegree(&mFront, mFront, toPlayer, cNum_exeFly_f1, Vec3f(0, 1, 0));
                mVelocity.scale(mSpeed, mFront);
            }
        }
    }

    if (MR::isBinded(this)) {
        kill();
    } else if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(6))) {
        kill();
    }
}

void ElectricPressureBullet::control() {
    if (MR::isStarPointerPointing2POnTriggerButton(this, "弱", true, false)) {
        kill();
    }
}

void ElectricPressureBullet::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && MR::sendMsgEnemyAttackElectric(pReceiver, pSender)) {
        kill();
    }
}
