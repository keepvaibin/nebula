#include "Game/Enemy/BallBeamer.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/RingBeam.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 12.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_isNearPlayer_1, "exeWait.isNearPlayer.1", 3000.0f);

namespace {
    const s32 sBeamNum = 12;
};  // namespace

namespace NrvBallBeamer {
    NEW_NERVE(BallBeamerNrvWait, BallBeamer, Wait);
    NEW_NERVE(BallBeamerNrvAttack, BallBeamer, Attack);
    NEW_NERVE(BallBeamerNrvInter, BallBeamer, Inter);
};  // namespace NrvBallBeamer

BallBeamer::BallBeamer(const char* pName) : LiveActor(pName), mBeams(), mIsSwitchOn() {
    lumacore::setClass(this, lumacore::kClassId< BallBeamer >);
    mHeadCollisionMtx.identity();
}

void BallBeamer::init(const TableIter& rIter) {
    initModelManagerWithAnm("BallBeamer", nullptr, false);
    MR::initDefaultPos(this, rIter);
    mPosition.y = mPosition.y - cNum_init_f1;
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(1);
    MR::addHitSensorPush(this, "Body", 8, cNum_init_f2, Vec3f(0.0f, -cNum_init_f3, 0.0f));
    MR::initShadowVolumeSphere(this, cNum_init_f4);
    initEffectKeeper(3, nullptr);
    initSound(2, false);
    initNerve(GET_NERVE(BallBeamer, BallBeamerNrvWait));
    makeActorAppeared();
    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnOffA(this, MR::Functor(this, &BallBeamer::syncSwitchOnA), MR::Functor(this, &BallBeamer::syncSwitchOffA));
    }

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor(this, &BallBeamer::syncSwitchOnB));
    }

    mBeams = new RingBeam*[::sBeamNum];
    f32 beamSpeed = cNum_init_f5;
    s32 beamLife = 530;
    s32 shadowMode = -1;
    MR::getPlacementArg0NoInit(rIter, &beamSpeed);
    MR::getPlacementArg1NoInit(rIter, &beamLife);
    MR::getPlacementArg2NoInit(rIter, &shadowMode);
    for (s32 i = 0; i < ::sBeamNum; i++) {
        mBeams[i] = new RingBeam("リングビーム", this, true, shadowMode == 0);
        mBeams[i]->init(rIter);
        mBeams[i]->setSpeed(beamSpeed);
        mBeams[i]->setLife(beamLife);
    }
    mHeadCollisionMtx.setInline(MR::getJointMtx(this, "Head"));
    MR::initCollisionParts(this, "Head", getSensor("Body"), mHeadCollisionMtx.toMtxPtr());
    MR::validateCollisionParts(this);
}

void BallBeamer::syncSwitchOnA() {
    mIsSwitchOn = true;
    BallBeamer::setupAttack();
}

void BallBeamer::syncSwitchOffA() {
    mIsSwitchOn = false;
}

void BallBeamer::syncSwitchOnB() {
    MR::deleteEffect(this, "Charge");
    MR::emitEffect(this, "Vanish");
    kill();
    for (s32 i = 0; i < ::sBeamNum; i++) {
        if (mBeams[i]) {
            mBeams[i]->kill();
        }
    }
}

void BallBeamer::setupAttack() {
    if (!MR::isValidSwitchA(this) || MR::isOnSwitchA(this)) {
        MR::invalidateClipping(this);
        if (!isNerve(GET_NERVE(BallBeamer, BallBeamerNrvAttack))) {
            setNerve(GET_NERVE(BallBeamer, BallBeamerNrvAttack));
        }
    }
}

bool BallBeamer::tryAttack() {
    for (s32 i = 0; i < ::sBeamNum; i++) {
        if (MR::isDead(mBeams[i])) {
            mBeams[i]->appear();
            return true;
        }
    }
    return false;
}

void BallBeamer::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startAction(this, "Wait");
    }

    if (MR::isNearPlayer(this, cNum_exeWait_isNearPlayer_1)) {
        BallBeamer::setupAttack();
    }
}

void BallBeamer::exeAttack() {
    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(6))) {
        setNerve(GET_NERVE(BallBeamer, BallBeamerNrvInter));
    } else {
        if (getNerveStep() % 120 >= 75 && getNerveStep() % 120 < 119) {
            MR::startLevelSound(this, "SE_EM_LV_RINGBEAM_CHARGE");
        }

        if (getNerveStep() % 120 == 75) {
            MR::emitEffect(this, "Charge");
        }

        if (getNerveStep() % 120 == 119) {
            MR::startAction(this, "Sign");
            MR::startSound(this, "SE_EM_BALLBEAMER_ROT_HEAD");
            BallBeamer::tryAttack();
        }
    }
}

void BallBeamer::exeInter() {

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        if (mIsSwitchOn) {
            setNerve(GET_NERVE(BallBeamer, BallBeamerNrvAttack));
        } else {
            setNerve(GET_NERVE(BallBeamer, BallBeamerNrvWait));
        }
    }
}

void BallBeamer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    MR::sendMsgPush(pReceiver, pSender);
}

bool BallBeamer::receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool BallBeamer::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgStarPieceReflect(msg);
}
