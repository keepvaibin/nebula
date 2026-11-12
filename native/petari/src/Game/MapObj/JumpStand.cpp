#include "Game/MapObj/JumpStand.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/NebulaInput.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvJumpStand {
    NEW_NERVE(JumpStandNrvWait, JumpStand, Wait);
    NEW_NERVE(JumpStandNrvTrampleBound, JumpStand, TrampleBound);
    NEW_NERVE(JumpStandNrvHipDropBound, JumpStand, HipDropBound);
    NEW_NERVE(JumpStandNrvStarPieceBound, JumpStand, StarPieceBound);
};  // namespace NrvJumpStand

namespace {
    // By JumpType.
    const char* const cJumpAnimName[] = {"TrampolineJumpLow", "TrampolineJumpMiddle", "TrampolineJumpHigh"};
};  // namespace

JumpStand::JumpStand(const char* pName) : LiveActor(pName), mBindedActor(), mIsMarioJumpingHigh() {
    lumacore::setClass(this, lumacore::kClassId< JumpStand >);
    mCollisionMtx.identity();
}

void JumpStand::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("JumpStand", nullptr, false);
    MR::connectToSceneMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addBodyMessageSensorMapObj(this);
    Vec3f binderOffs;
    binderOffs.set(0.0f, 100.0f, 0.0f);
    MR::addHitSensorPriorBinder(this, "binder", 4, 200.0f, binderOffs);
    MR::invalidateHitSensor(this, "binder");
    mCollisionMtx.set(MR::getJointMtx(this, "JumpStandJoint03"));
    MR::initCollisionParts(this, "JumpStand", getSensor("body"), mCollisionMtx);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::setGroupClipping(this, rIter, 16);
    MR::initShadowFromCSV(this, "Shadow");
    MR::onCalcGravity(this);
    initNerve(GET_NERVE(JumpStand, JumpStandNrvWait));
    makeActorAppeared();
}

void JumpStand::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startBck(this, "Wait");
    }
}

void JumpStand::exeTrampleBound() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Bound");
        MR::startSound(this, "SE_OJ_JUMP_STAND_LAND_S");
    }

    if (MR::isLessEqualStep(this, 5) && MR::isPressedJump(0)) {
        mIsMarioJumpingHigh = true;
        MR::startBck(this, "BoundJump");
        MR::setBckFrame(this, getNerveStep());
    }

    // A jump press in the first 5 steps bounces higher.
    if (MR::isStep(this, 5)) {
        if (mIsMarioJumpingHigh) {
            MR::startSound(this, "SE_OJ_JUMP_STAND_BOUND_M");
            MR::startSoundPlayer("SE_PV_JUMP_L");
            endBindAndShootUp(32.0f, Jump_Middle);
        } else {
            MR::startSound(this, "SE_OJ_JUMP_STAND_BOUND_S");
            MR::startSoundPlayer("SE_PV_JUMP_M");
            endBindAndShootUp(26.0f, Jump_Low);
        }

        MR::validateCollisionParts(this);
    }

    if (MR::isStep(this, 10)) {
        MR::validateCollisionParts(this);
    }

    if (MR::isBckStopped(this)) {
        mIsMarioJumpingHigh = false;
        setNerve(GET_NERVE(JumpStand, JumpStandNrvWait));
    }
}

void JumpStand::exeHipDropBound() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "BoundHipDrop");
        MR::stopSound(mBindedActor, "SE_PM_HIPDROP", 0);
        MR::startSound(this, "SE_OJ_JUMP_STAND_LAND_L");
    }

    // Presses the player down into the stand for 15 steps, 6 a step easing to 5.
    if (MR::isLessStep(this, 15) && mBindedActor != nullptr) {
        f32 pressDepth = (6.0f - MR::calcNerveEaseOutRate(this, 15));
        Vec3f up;
        MR::calcUpVec(&up, this);
        mBindedActor->mPosition.scaleAdd(-pressDepth, up, mBindedActor->mPosition);
    }

    if (MR::isStep(this, 15)) {
        MR::startSound(this, "SE_OJ_JUMP_STAND_BOUND_L");
        MR::startSoundPlayer("SE_PV_JUMP_JOY");
        endBindAndShootUp(43.0f, Jump_High);
    }

    if (MR::isStep(this, 20)) {
        MR::validateCollisionParts(this);
    }

    if (MR::isBckStopped(this)) {
        MR::validateCollisionParts(this);
        setNerve(GET_NERVE(JumpStand, JumpStandNrvWait));
    }
}

void JumpStand::exeStarPieceBound() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Bound");
        MR::startSound(this, "SE_OJ_JUMP_STAND_LAND_S");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpStand, JumpStandNrvWait));
    }
}

void JumpStand::control() {
    Vec3f pos;
    MR::copyJointPos(this, "JumpStandJoint03", &pos);
    mCollisionMtx.setTrans(pos);
}

bool JumpStand::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(JumpStand, JumpStandNrvTrampleBound)) || isNerve(GET_NERVE(JumpStand, JumpStandNrvHipDropBound))) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(JumpStand, JumpStandNrvStarPieceBound));
        return true;
    }

    return false;
}

bool JumpStand::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgUpdateBaseMtx(msg) && mBindedActor != nullptr) {
        updateBindActorMtx();
        return true;
    }

    if (MR::isMsgFloorTouch(msg)) {
        MR::validateHitSensor(this, "binder");
        return true;
    }

    // The player landing on it starts a bounce: a hip drop lands deep, anything else a trample.
    if (!MR::isMsgAutoRushBegin(msg) || !MR::isSensorPlayer(pSender) || !MR::isOnPlayer(getSensor("body"))) {
        return false;
    }

    bool isHipDrop = MR::isPlayerHipDropLand();
    if (!tryToStartBind(pSender)) {
        return false;
    }

    if (isHipDrop) {
        setNerve(GET_NERVE(JumpStand, JumpStandNrvHipDropBound));
    } else {
        setNerve(GET_NERVE(JumpStand, JumpStandNrvTrampleBound));
    }

    return true;
}

bool JumpStand::tryToStartBind(HitSensor* pSensor) {
    if (mBindedActor != nullptr) {
        return false;
    }

    MR::tryRumblePadMiddle(this, PLAYER_1);
    MR::invalidateCollisionParts(this);
    MR::invalidateClipping(this);
    mBindedActor = pSensor->mHost;
    MR::offBind(mBindedActor);
    return true;
}

void JumpStand::updateBindActorMtx() {
    // The held player turns toward the stick.
    Pos3f mtx;
    mtx.identity();
    Vec3f padDir;
    MR::calcPlayerWorldPadDir(&padDir, MR::getMoveStickX(0), MR::getMoveStickY(0));
    MR::calcMtxFromGravityAndZAxis(&mtx, mBindedActor, mGravity, padDir);
    MR::blendMtxRotate(mBindedActor->getBaseMtx(), mtx, 0.40f, mtx);
    mtx.setTrans(mBindedActor->mPosition);
    MR::setBaseTRMtx(mBindedActor, mtx);
}

// Shoots the player up at speed (along the stand for a high jump, against gravity otherwise), steered by up to
// 3 across the jump by the stick.
void JumpStand::endBindAndShootUp(f32 speed, JumpType type) {
    if (MR::isPlayerInRush()) {
        Vec3f jumpVel;
        if (type == Jump_High) {
            MR::calcUpVec(&jumpVel, this);
        } else {
            jumpVel.negate(mGravity);
        }

        jumpVel *= speed;
        f32 stickLen = MR::sqrt(MR::getMoveStickX(PLAYER_1) * MR::getMoveStickX(PLAYER_1) +
                                MR::getMoveStickY(PLAYER_1) * MR::getMoveStickY(PLAYER_1));
        if (stickLen > 0.1f) {
            Vec3f jumpDir;
            MR::normalize(jumpVel, &jumpDir);
            Vec3f padDir;
            MR::calcPlayerWorldPadDir(&padDir, MR::getMoveStickX(PLAYER_1), MR::getMoveStickY(PLAYER_1));
            Vec3f steer;
            steer.scale(3.0f * stickLen, padDir);
            steer.orthogonalize(jumpDir);
            jumpVel.add(steer);
        }

        MR::startBckPlayer(cJumpAnimName[type]);
        MR::endBindAndPlayerJump(this, jumpVel, 0);

        if (type != Jump_High) {
            MR::becomePlayerNormalJumpStatus();
        }

        MR::invalidateHitSensor(this, "binder");
    }

    mBindedActor = nullptr;
}
