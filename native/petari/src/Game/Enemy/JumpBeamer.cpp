#include "Game/Enemy/JumpBeamer.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/JumpGuarder.hpp"
#include "Game/Enemy/RingBeam.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cBodyRadius = 145.0f;
    const f32 cBodyHopRadius = 120.0f;
    // The shadow grows from the hidden to the up radius as it rises.
    const f32 cShadowHideRadius = 110.0f;
    const f32 cShadowRiseRadius = 30.0f;
    // Comes up when the player is this near, and hides again past the leave distance.
    const f32 cAppearDistance = 3000.0f;
    const f32 cLeaveDistance = 3200.0f;
    const f32 cGroupHeight = 500.0f;
    const s32 cHopWaitSteps = NebulaFrameClock::stepsOfSeconds(5);
    // One beam each interval, charged over the interval; then it closes.
    const s32 cBeamInterval = 80;
    const s32 cOpenSteps = cBeamInterval * JumpBeamer::sBeamCount;
    const s32 cInterSteps = 80;
    const f32 cDefaultBeamSpeed = 20.0f;
    const s32 cDefaultBeamLife = 100;
};  // namespace

namespace NrvJumpBeamer {
    NEW_NERVE(JumpBeamerNrvHide, JumpBeamer, Hide);
    NEW_NERVE(JumpBeamerNrvUp, JumpBeamer, Up);
    NEW_NERVE(JumpBeamerNrvWait, JumpBeamer, Wait);
    NEW_NERVE(JumpBeamerNrvDown, JumpBeamer, Down);
    NEW_NERVE(JumpBeamerNrvHopStart, JumpBeamer, HopStart);
    NEW_NERVE(JumpBeamerNrvHopWait, JumpBeamer, HopWait);
    NEW_NERVE(JumpBeamerNrvHopJump, JumpBeamer, HopJump);
    NEW_NERVE(JumpBeamerNrvHopEnd, JumpBeamer, HopEnd);
    NEW_NERVE(JumpBeamerNrvPreOpen, JumpBeamer, PreOpen);
    NEW_NERVE(JumpBeamerNrvOpen, JumpBeamer, Open);
    NEW_NERVE(JumpBeamerNrvClose, JumpBeamer, Close);
    NEW_NERVE(JumpBeamerNrvInter, JumpBeamer, Inter);
};  // namespace NrvJumpBeamer

// A spring-headed pillar that rises from the ground with its group and fires rings of beams; trampled, its head is a
// spring.
JumpBeamer::JumpBeamer(const char* pName) : JumpEmitter(pName), mBeams() {
    lumacore::setClass(this, lumacore::kClassId< JumpBeamer >);
}

// The head follows the body's top joint, turned and scaled by the head model.
void JumpBeamer::control() {
    PartsModel* pHead = mHeadModel;
    Mtx34f headLocal;
    MR::makeMtxTRS(headLocal, Vec3f(0.0f, 0.0f, 0.0f), pHead->mRotation, pHead->mScale);
    mHeadMtx.setInline(mBodyJointMtx);
    Mtx34f headMtx;
    headMtx.concat(mHeadMtx, headLocal);
    mHeadMtx.setInline(headMtx);
    updateEventCamera();
}

// Arg 0 is the beams' speed, arg 1 their life.
void JumpBeamer::init(const TableIter& rIter) {
    initModelManagerWithAnm("JumpBeamerBody", nullptr, false);
    mHeadModel = MR::createPartsModelMapObjStrongLight(this, "ジャンプビーマー頭", "JumpBeamerHead", mHeadMtx);
    MR::initLightCtrl(mHeadModel);
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addHitSensorMtx(this, "Jump", ATYPE_PLAYER_AUTO_JUMP, 8, cBodyRadius, MR::getJointMtx(mHeadModel, "SpringJoint3"), Vec3f(0.0f, -100.0f, 0.0f));
    MR::addHitSensorMtxEnemy(this, "Body", 8, cBodyRadius, MR::getJointMtx(this, "Body"), Vec3f(0.0f, 35.0f, 0.0f));
    getSensor("Body")->setType(ATYPE_BEGOMAN);
    getSensor("Body")->validate();
    getSensor("Jump")->invalidate();
    MR::initShadowVolumeSphere(this, 140.0f);
    MR::invalidateShadow(this, nullptr);
    initEffectKeeper(1, nullptr);
    initSound(8, false);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHide));
    MR::startBckWithInterpole(this, "Down", 0);
    MR::setBckFrame(this, MR::getBckCtrl(this)->getEnd() - 1);
    MR::calcAnimDirect(this);
    mBodyJointMtx = MR::getJointMtx(this, "Top");
    MR::useStageSwitchReadA(this, rIter);

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOffB(this, MR::Functor(this, &JumpBeamer::syncSwitchOffB));
    }

    initEventCamera(rIter);
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    makeActorAppeared();

    f32 beamSpeed = cDefaultBeamSpeed;
    s32 beamLife = cDefaultBeamLife;
    MR::getPlacementArg0NoInit(rIter, &beamSpeed);
    MR::getPlacementArg1NoInit(rIter, &beamLife);

    for (s32 i = 0; i < sBeamCount; i++) {
        mBeams[i] = new RingBeam("リングビーム", this, false, false);
        mBeams[i]->init(rIter);
        mBeams[i]->setSpeed(beamSpeed);
        mBeams[i]->setLife(beamLife);
    }
}

void JumpBeamer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

// Trampling the spring launches the player; trampling the body or spinning it makes it hop.
bool JumpBeamer::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgPlayerTrample(msg)) {
        if (pReceiver->isType(ATYPE_PLAYER_AUTO_JUMP)) {
            Vec3f up;
            MR::calcUpVec(&up, this);
            MR::setPlayerJumpVec(up);
            startEventCamera();
            setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHopJump));
        } else {
            setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHopStart));
        }

        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::forceJumpPlayer(-mGravity);
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (!isNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHopStart))) {
            setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHopStart));
        }

        return true;
    }

    return MR::isMsgStarPieceReflect(msg);
}

void JumpBeamer::syncSwitchOffB() {
    setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvInter));

    for (s32 i = 0; i < sBeamCount; i++) {
        mBeams[i]->kill();
    }
}

bool JumpBeamer::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_GROUP_ATTACK) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvUp));
        return true;
    }

    if (msg == ACTMES_GROUP_HIDE) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvDown));
        return true;
    }

    return false;
}

// Switch B, when it has one, holds it still and closed while off.
bool JumpBeamer::isHeldBySwitchB() const {
    return MR::isValidSwitchB(this) && !MR::isOnSwitchB(this);
}

void JumpBeamer::exeHide() {
    if (!isHeldBySwitchB()) {
        updateRotate();
    }

    if (MR::isFirstStep(this)) {
        MR::startBck(mHeadModel, "Wait");
        MR::startBrk(mHeadModel, "Green");
        MR::validateClipping(this);
        MR::setShadowVolumeSphereRadius(this, nullptr, cShadowHideRadius);
    }

    if (MR::enableGroupAttack(this, cAppearDistance, cGroupHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_ATTACK, this, getSensor("Body"), "Body");
    }
}

void JumpBeamer::exeUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Up");
        MR::startSound(this, "SE_EM_JGUARDER_APPEAR");
        MR::validateShadow(this, nullptr);
    }

    s32 end = MR::getBckCtrl(this)->getEnd();
    f32 frame = MR::getBckFrame(this);
    MR::setShadowVolumeSphereRadius(this, nullptr, (cShadowHideRadius + (cShadowRiseRadius * (frame / (end)))));

    if (MR::isBckStopped(this)) {
        MR::invalidateShadow(this, nullptr);
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvWait));
    }
}

void JumpBeamer::exeWait() {
    updateRotate();

    if (!MR::enableGroupAttack(this, cLeaveDistance, cGroupHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_HIDE, this, getSensor("Body"), "Body");
    } else if (!isHeldBySwitchB()) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvPreOpen));
    }
}

void JumpBeamer::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
        MR::startSound(this, "SE_EM_JGUARDER_HIDE");
    }

    s32 end = MR::getBckCtrl(this)->getEnd();
    f32 frame = MR::getBckFrame(this);
    MR::setShadowVolumeSphereRadius(this, nullptr, (cShadowHideRadius + (cShadowRiseRadius * (1.0f - frame / (end)))));

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHide));
    }
}

void JumpBeamer::exeHopStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mHeadModel, "HopStart");
        MR::startBrk(mHeadModel, "OnAndOff");
        MR::startBck(this, "Damage");
        MR::deleteEffect(this, "Charge");
        MR::startSound(this, "SE_EM_JGUARDER_HIT");
        getSensor("Body")->mRadius = cBodyHopRadius;
    }

    MR::startLevelSound(this, "SE_EM_LV_JGUARDER_SHAKE");

    if (MR::isBckStopped(mHeadModel)) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHopWait));
    }
}

void JumpBeamer::exeHopWait() {
    if (MR::isFirstStep(this)) {
        MR::stopBck(this);
        MR::startBck(mHeadModel, "HopWait");
        getSensor("Jump")->validate();
    }

    MR::startLevelSound(this, "SE_EM_LV_JGUARDER_SHAKE");

    if (MR::isStep(this, cHopWaitSteps)) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHopEnd));
    }
}

void JumpBeamer::exeHopJump() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mHeadModel, "HopJump");
        MR::startSound(this, "SE_EM_JGUARDER_TRAMPLE");
    }

    if (MR::isBckStopped(mHeadModel)) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvHopWait));
    }
}

void JumpBeamer::exeHopEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HopEnd");
        MR::startBck(mHeadModel, "HopEnd");
        MR::startBrk(mHeadModel, "Green");
        MR::startSound(this, "SE_EM_JGUARDER_CLOSE_SPRING");
    }

    if (MR::isBckStopped(this) && MR::isBckStopped(mHeadModel)) {
        getSensor("Body")->mRadius = cBodyRadius;
        getSensor("Jump")->invalidate();
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvWait));
    }
}

void JumpBeamer::exePreOpen() {
    updateRotate();

    if (!MR::enableGroupAttack(this, cLeaveDistance, cGroupHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_HIDE, this, getSensor("Body"), "Body");
    } else if (MR::isFirstStep(this)) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvOpen));
    }
}

void JumpBeamer::exeOpen() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Open");
    }

    if (MR::isGreaterEqualStep(this, cOpenSteps)) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvClose));
        return;
    }

    s32 intervalStep = getNerveStep() % cBeamInterval;

    if (intervalStep == 0) {
        MR::emitEffect(this, "Charge");
    }

    if (intervalStep == cBeamInterval - 1) {
        MR::deleteEffect(this, "Charge");
        mBeams[getNerveStep() / cBeamInterval]->appear();
    }
}

void JumpBeamer::exeClose() {
    MR::startLevelSound(this, "SE_EM_LV_JGUARDER_SHUTTER_CLOSE");

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvInter));
    }
}

// Waits for every beam to finish before it can open again.
void JumpBeamer::exeInter() {
    if (!isHeldBySwitchB()) {
        updateRotate();
    }

    if (!MR::enableGroupAttack(this, cLeaveDistance, cGroupHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_HIDE, this, getSensor("Body"), "Body");
        return;
    }

    if (!MR::isGreaterEqualStep(this, cInterSteps)) {
        return;
    }

    for (s32 i = 0; i < sBeamCount; i++) {
        if (!MR::isDead(mBeams[i])) {
            return;
        }
    }

    setNerve(GET_NERVE(JumpBeamer, JumpBeamerNrvWait));
}
