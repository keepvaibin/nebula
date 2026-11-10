#include "Game/Boss/DinoPackunTailPart.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunAction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_DinoPackunTailPart_f1, "DinoPackunTailPart.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 3.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_addVelocityToGravity_1, "exeWait.addVelocityToGravity.1", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_attenuateVelocity_1, "exeWait.attenuateVelocity.1", 0.88f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_1, "exeWait.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_2, "exeWait.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_reboundVelocityFromCollision_3, "exeWait.reboundVelocityFromCollision.3", 1.0f);

namespace NrvDinoPackunTailPart {
    NEW_NERVE(DinoPackunTailPartNrvWait, DinoPackunTailPart, Wait);
    NEW_NERVE(DinoPackunTailPartNrvLockPosition, DinoPackunTailPart, LockPosition);
};  // namespace NrvDinoPackunTailPart

DinoPackunTailPart::DinoPackunTailPart(const char* pName, DinoPackun* pParent) : DinoPackunTailNode(pName, pParent) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackunTailPart >);
    mRadius = cNum_DinoPackunTailPart_f1;
}

void DinoPackunTailPart::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE(DinoPackunTailPart, DinoPackunTailPartNrvWait));
    MR::onCalcGravity(this);
    initBinder(mRadius, 0.0f, 0);
    MR::connectToSceneEnemyDecorationMovement(this);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f1 + mRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void DinoPackunTailPart::control() {
}

JointController* DinoPackunTailPart::createJointControllerOwn(LiveActor* pActor, const char* pJointName) {
    return MR::createJointDelegator(static_cast< DinoPackunTailNode* >(this),pActor, &DinoPackunTailNode::turnJointLocalXDir, &DinoPackunTailNode::calcJointScale,
                                    pJointName);
}

void DinoPackunTailPart::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    mParent->attackSensorTail(pSender, pReceiver);
    bool isPushed = MR::sendMsgPush(pReceiver, pSender);
    if (MR::isSensorPlayer(pReceiver) && isPushed) {
        // Gives way to the player, up to 3 a step.
        Vec3f away;
        MR::calcSensorDirectionNormalize(&away, pReceiver, pSender);

        if (mVelocity.dot(away) < cNum_attackSensor_f1) {
            mVelocity.add(away);
        }
    }
}

bool DinoPackunTailPart::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::sendMsgAwayJump(pSender, pReceiver);
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        return true;
    }

    return mParent->receiveMsgPlayerAttackTail(msg, pSender, pReceiver);
}

void DinoPackunTailPart::requestLockPosition() {
    MR::zeroVelocity(this);
    setNerve(GET_NERVE(DinoPackunTailPart, DinoPackunTailPartNrvLockPosition));
    MR::offBind(this);
}

void DinoPackunTailPart::requestUnLockPosition() {
    setNerve(GET_NERVE(DinoPackunTailPart, DinoPackunTailPartNrvWait));
    MR::onBind(this);
}

void DinoPackunTailPart::exeLockPosition() {
}

void DinoPackunTailPart::exeWait() {
    MR::addVelocityToGravity(this, cNum_exeWait_addVelocityToGravity_1);
    MR::attenuateVelocity(this, cNum_exeWait_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_exeWait_reboundVelocityFromCollision_1, cNum_exeWait_reboundVelocityFromCollision_2, cNum_exeWait_reboundVelocityFromCollision_3);
}
