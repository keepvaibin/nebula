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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShot_f1, "exeShot.f1", 150.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeShot_addVelocityToGravity_1, "exeShot.addVelocityToGravity.1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShot_attenuateVelocity_1, "exeShot.attenuateVelocity.1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShot_reboundVelocityFromCollision_1, "exeShot.reboundVelocityFromCollision.1", 0.80f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShot_reboundVelocityFromCollision_2, "exeShot.reboundVelocityFromCollision.2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShot_reboundVelocityFromCollision_3, "exeShot.reboundVelocityFromCollision.3", 0.40f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeGround_1, "exeGround.1", 20);

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
    MR::addHitSensorEnemyAttack(this, "attack", 8, cNum_init_f1, attackOffs);
    Vec3f starOffs;
    starOffs.set(0.0f);
    MR::initStarPointerTarget(this, cNum_init_f2, starOffs);
    initBinder(cNum_init_f3, 0.0f, 0);
    initNerve(GET_NERVE(DinoPackunFire, DinoPackunFireNrvShot));
    MR::startBtk(this, "DinoPackunFireBall");
    MR::initShadowVolumeSphere(this, cNum_init_f4);
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

    MR::rotateQuatRollBall(&mRollQuat, mVelocity, -mGravity, (cNum_exeShot_f1 * mScale.x));
    MR::addVelocityToGravity(this, cNum_exeShot_addVelocityToGravity_1);
    MR::attenuateVelocity(this, cNum_exeShot_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_exeShot_reboundVelocityFromCollision_1, cNum_exeShot_reboundVelocityFromCollision_2, cNum_exeShot_reboundVelocityFromCollision_3);
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
    if (MR::isGreaterStep(this, cStep_exeGround_1)) {
        MR::startSound(this, "SE_BM_D_PAKKUN_FIREBALL_L_END");
        kill();
    }
}
