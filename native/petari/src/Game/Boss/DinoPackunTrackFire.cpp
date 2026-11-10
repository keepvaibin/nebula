#include "Game/Boss/DinoPackunTrackFire.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 60.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_addVelocityToGravity_1, "exeWait.addVelocityToGravity.1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_attenuateVelocity_1, "exeWait.attenuateVelocity.1", 0.992f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeGround_1, "exeGround.1", 75);

namespace NrvDinoPackunFire {
    NEW_NERVE(DinoPackunTrackFireNrvWait, DinoPackunTrackFire, Wait);
    NEW_NERVE(DinoPackunTrackFireNrvGround, DinoPackunTrackFire, Ground);
};  // namespace NrvDinoPackunFire

DinoPackunTrackFireHolder::DinoPackunTrackFireHolder(s32 max) : DeriveActorGroup< DinoPackunTrackFire >("シッポ跡炎管理", max) {
    DinoPackunTrackFire* pTrackFire;

    for (s32 i = 0; i < max; i++) {
        pTrackFire = new DinoPackunTrackFire("シッポ跡炎");
        pTrackFire->initWithoutIter();
        registerActor(pTrackFire);
    }
}

DinoPackunTrackFire::DinoPackunTrackFire(const char* pName) : LiveActor(pName) {
    mMtx.identity();
}

void DinoPackunTrackFire::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneEnemyMovement(this);
    initHitSensor(1);
    Vec3f sensorOffs;
    sensorOffs.x = 0.0f;
    sensorOffs.y = 0.0f;
    sensorOffs.z = 0.0f;
    MR::addHitSensorEnemyAttack(this, "attack", 8, cNum_init_f1, sensorOffs);
    Vec3f spOffs;
    spOffs.x = 0.0f;
    spOffs.y = 0.0f;
    spOffs.z = 0.0f;
    MR::initStarPointerTarget(this, cNum_init_f2, spOffs);
    initBinder(cNum_init_f3, 0.0f, 0);
    initNerve(GET_NERVE(DinoPackunFire, DinoPackunTrackFireNrvWait));
    MR::onCalcGravity(this);
    MR::invalidateClipping(this);
    initEffectKeeper(0, "DinoPackunTrackFire");
    initSound(4, false);
    makeActorDead();
}

void DinoPackunTrackFire::control() {
    MR::makeMtxUpNoSupportPos(&mMtx, -mGravity, mPosition);

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        kill();
    }
}

void DinoPackunTrackFire::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {

    MR::sendMsgEnemyAttackFire(pReceiver, pSender);
}

bool DinoPackunTrackFire::receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void DinoPackunTrackFire::appearAndSetPos(const Vec3f& rPos) {
    mPosition.set(rPos);
    MR::zeroVelocity(this);
    MR::startSound(this, "SE_BM_D_PAKKUN_FIREBALL_S_ON");
    setNerve(GET_NERVE(DinoPackunFire, DinoPackunTrackFireNrvWait));
    appear();
}

void DinoPackunTrackFire::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "TrackFire");
    }

    MR::addVelocityToGravity(this, cNum_exeWait_addVelocityToGravity_1);
    MR::attenuateVelocity(this, cNum_exeWait_attenuateVelocity_1);
    if (MR::isBindedGround(this)) {
        setNerve(GET_NERVE(DinoPackunFire, DinoPackunTrackFireNrvGround));
        MR::zeroVelocity(this);
    } else if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(5))) {
        MR::deleteEffect(this, "TrackFire");
        kill();
    }
}

void DinoPackunTrackFire::exeGround() {
    MR::zeroVelocity(this);

    if (MR::isGreaterStep(this, cStep_exeGround_1)) {
        MR::deleteEffect(this, "TrackFire");
        kill();
    }
}
