#include "Game/Boss/DinoPackunFire.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvDinoPackunFire {
    NEW_NERVE(DinoPackunFireNrvShot, DinoPackunFire, Shot);
    NEW_NERVE(DinoPackunFireNrvGround, DinoPackunFire, Ground);
};  // namespace NrvDinoPackunFire

DinoPackunFireHolder::DinoPackunFireHolder(s32 numMax) : DeriveActorGroup< DinoPackunFire >("ディノパックン炎管理", numMax) {
    DinoPackunFire* pFire;

    for (s32 i = 0; i < numMax; i++) {
        pFire = new DinoPackunFire("ディノパックン炎");
        pFire->initWithoutIter();
        registerActor(pFire);
    }
}

DinoPackunFire::DinoPackunFire(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackunFire >);
    mRollQuat.x = 0.0f;
    mRollQuat.y = 0.0f;
    mRollQuat.z = 0.0f;
    mRollQuat.w = 1.0f;
}

void DinoPackunFire::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("DinoPackunFireBall", nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(1);
    Vec3f attackOffs;
    attackOffs.set(0.0f, 0.0f, 0.0f);
    MR::addHitSensorEnemyAttack(this, "attack", 8, 150.0f, attackOffs);
    Vec3f starOffs;
    starOffs.set(0.0f);
    MR::initStarPointerTarget(this, 150.0f, starOffs);
    initBinder(150.0f, 0.0f, 0);
    initNerve(GET_NERVE(DinoPackunFire, DinoPackunFireNrvShot));
    MR::startBtk(this, "DinoPackunFireBall");
    MR::initShadowVolumeSphere(this, 150.0f);
    MR::onCalcShadow(this, nullptr);
    MR::onCalcGravity(this);
    MR::invalidateClipping(this);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    makeActorDead();
}

void DinoPackunFire::kill() {
    MR::emitEffect(this, "Break");
    LiveActor::kill();
}

void DinoPackunFire::control() {
}

void DinoPackunFire::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRollQuat);
}

void DinoPackunFire::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {

    MR::sendMsgEnemyAttackFire(pReceiver, pSender);
}

void DinoPackunFire::appearShot(const Vec3f& rPos, const Vec3f& rVelocity) {
    mPosition.x = rPos.x;
    mPosition.y = rPos.y;
    mPosition.z = rPos.z;
    setNerve(GET_NERVE(DinoPackunFire, DinoPackunFireNrvShot));
    appear();
    MR::setVelocity(this, rVelocity);
}

void DinoPackunFire::exeShot() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BM_D_PAKKUN_FIREBALL_L_SHOOT");
        MR::startAction(this, "Move");
    }

    MR::rotateQuatRollBall(&mRollQuat, mVelocity, -mGravity, (150.0f * mScale.x));
    MR::addVelocityToGravity(this, 2.0f);
    MR::attenuateVelocity(this, 0.98f);
    MR::reboundVelocityFromCollision(this, 0.80f, 15.0f, 0.40f);
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2)) && MR::isBindedGround(this)) {
        setNerve(GET_NERVE(DinoPackunFire, DinoPackunFireNrvGround));
        MR::zeroVelocity(this);
    } else {
        MR::startLevelSound(this, "SE_BM_LV_D_PAKKUN_FIREBALL_L");
        MR::sendMsgEnemyAttackToBindedSensor(this, getSensor(nullptr));
        if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(7500))) {
            MR::startSound(this, "SE_BM_D_PAKKUN_FIREBALL_L_END");
            kill();
        }
    }
}

void DinoPackunFire::exeGround() {
    MR::zeroVelocity(this);
    MR::startLevelSound(this, "SE_BM_LV_D_PAKKUN_FIREBALL_L");
    MR::sendMsgEnemyAttackToBindedSensor(this, getSensor(nullptr));
    if (MR::isGreaterStep(this, 20)) {
        MR::startSound(this, "SE_BM_D_PAKKUN_FIREBALL_L_END");
        kill();
    }
}
