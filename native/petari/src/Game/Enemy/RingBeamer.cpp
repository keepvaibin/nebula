#include "Game/Enemy/RingBeamer.hpp"
#include "Game/Enemy/JumpGuarder.hpp"
#include "Game/Enemy/RingBeam.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // One beam per interval of the attack.
    const s32 sBeamNum = 3;
    const s32 sBeamInterval = 80;
};  // namespace

namespace NrvRingBeamer {
    NEW_NERVE(RingBeamerNrvWait, RingBeamer, Wait);
    NEW_NERVE(RingBeamerNrvAttack, RingBeamer, Attack);
    NEW_NERVE(RingBeamerNrvInter, RingBeamer, Inter);
};  // namespace NrvRingBeamer

RingBeamer::RingBeamer(const char* pName) : LiveActor(pName), mBeams() {
    lumacore::setClass(this, lumacore::kClassId< RingBeamer >);
}

void RingBeamer::init(const TableIter& rIter) {
    initModelManagerWithAnm("RingBeamer", nullptr, false);
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addHitSensorPush(this, "Body", 8, 160.0f, Vec3f(0.0f, 80.0f, 0.0f));
    MR::addHitSensorMtxEnemyAttack(this, "Head", 8, 130.0f, MR::getJointMtx(this, "Top"), Vec3f(0.0f, 110.0f, 0.0f));
    MR::initShadowVolumeSphere(this, 120.0f);
    initEffectKeeper(1, nullptr);
    initSound(8, false);
    initNerve(GET_NERVE(RingBeamer, RingBeamerNrvWait));
    makeActorAppeared();
    MR::useStageSwitchReadA(this, rIter);
    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOffB(this, MR::Functor(this, &RingBeamer::syncSwitchOffB));
    }
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    mBeams = new RingBeam*[::sBeamNum];
    f32 beamSpeed = 20.0f;
    s32 beamLife = 100;
    MR::getPlacementArg0NoInit(rIter, &beamSpeed);
    MR::getPlacementArg1NoInit(rIter, &beamLife);
    for (s32 i = 0; i < ::sBeamNum; i++) {
        mBeams[i] = new RingBeam("リングビーム", this, false, false);
        mBeams[i]->init(rIter);
        mBeams[i]->setSpeed(beamSpeed);
        mBeams[i]->setLife(beamLife);
    }
    MR::startBckWithInterpole(this, "Open", 0);
    MR::setBckFrameAndStop(this, 0.0);
    MR::calcAnimDirect(this);
}

void RingBeamer::syncSwitchOffB() {
    setNerve(GET_NERVE(RingBeamer, RingBeamerNrvInter));

    for (s32 i = 0; i < ::sBeamNum; i++) {
        mBeams[i]->kill();
    }
}

void RingBeamer::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }

    if (MR::enableGroupAttack(this, 3000.0f, 500.0f)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_ATTACK, this, getSensor("Body"), "Body");
    }
}

void RingBeamer::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Open");
    }

    if (MR::isGreaterEqualStep(this, ::sBeamNum * ::sBeamInterval)) {
        setNerve(GET_NERVE(RingBeamer, RingBeamerNrvInter));
        return;
    }

    if (getNerveStep() % ::sBeamInterval >= 0 && getNerveStep() % ::sBeamInterval < ::sBeamInterval - 1) {
        MR::startLevelSound(this, "SE_EM_LV_RINGBEAM_CHARGE");
    }

    if (getNerveStep() % ::sBeamInterval == 0) {
        MR::emitEffect(this, "Charge");
    }

    if (getNerveStep() % ::sBeamInterval == ::sBeamInterval - 1) {
        MR::deleteEffect(this, "Charge");

        if (!MR::enableGroupAttack(this, 3200.0f, 500.0f)) {
            MR::sendMsgToGroupMember(ACTMES_GROUP_HIDE, this, getSensor("Body"), "Body");
        } else {
            mBeams[getNerveStep() / ::sBeamInterval]->appear();
        }
    }
}

void RingBeamer::exeInter() {
    if (MR::isGreaterEqualStep(this, 80)) {
        for (s32 i = 0; i < ::sBeamNum; i++) {
            if (!MR::isDead(mBeams[i])) {
                return;
            }
        }

        setNerve(GET_NERVE(RingBeamer, RingBeamerNrvWait));
    }
}

void RingBeamer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver) && MR::isSensorEnemyAttack(pSender)) {
        MR::sendMsgEnemyAttackStrong(pReceiver, pSender);
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool RingBeamer::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_GROUP_ATTACK) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(RingBeamer, RingBeamerNrvAttack));

        return true;
    }

    if (msg == ACTMES_GROUP_HIDE) {
        setNerve(GET_NERVE(RingBeamer, RingBeamerNrvInter));

        return true;
    }

    return false;
}

bool RingBeamer::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgJetTurtleAttack(msg)) {
        return true;
    }

    return MR::isMsgStarPieceReflect(msg);
}
