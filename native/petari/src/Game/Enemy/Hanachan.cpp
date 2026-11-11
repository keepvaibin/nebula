#include "Game/Enemy/Hanachan.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f11, "init.f11", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f1, "receiveMsgPush.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f2, "receiveMsgPush.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f1, "exeWalk.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrample_f1, "exeTrample.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBecomeAngry_f1, "exeBecomeAngry.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryPursue_f1, "exeAngryPursue.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryEnd_f1, "exeAngryEnd.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWallHitEnd_f1, "exeWallHitEnd.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOverturn_f1, "exeOverturn.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOverturnBound_f1, "exeOverturnBound.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_f1, "exeRecover.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHipDropped_f1, "exeHipDropped.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHipDropped_f2, "exeHipDropped.f2", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f12, "init.f12", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f13, "init.f13", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f14, "init.f14", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f15, "init.f15", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f16, "init.f16", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f17, "init.f17", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f18, "init.f18", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f19, "init.f19", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryPursue_f2, "exeAngryPursue.f2", 0.08f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryEnd_f2, "exeAngryEnd.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryEnd_f3, "exeAngryEnd.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcNearestInfectionId_f1, "calcNearestInfectionId.f1", 9999999.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setNerveBlow_f1, "setNerveBlow.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_applyPlayerHipDropReaction_f1, "applyPlayerHipDropReaction.f1", 999999.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveHeadAlongRail_f1, "moveHeadAlongRail.f1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveHeadAlongRail_f2, "moveHeadAlongRail.f2", 0.04f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveHeadAlongRail_f3, "moveHeadAlongRail.f3", 0.08f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveHeadAlongRail_f4, "moveHeadAlongRail.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveHeadAlongRail_f5, "moveHeadAlongRail.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveHeadToPlayer_f1, "moveHeadToPlayer.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveHeadToPlayer_f2, "moveHeadToPlayer.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveBodyAlongHead_f1, "moveBodyAlongHead.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveBodyAlongHead_f2, "moveBodyAlongHead.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveBodyAlongHead_f3, "moveBodyAlongHead.f3", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveBodyAlongHead_f4, "moveBodyAlongHead.f4", 0.5f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_endStarPointerBind_setBckRate_1, "endStarPointerBind.setBckRate.1", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeBlow_1, "exeBlow.1", 15);

namespace {
    NEBULA_TUNABLE(f32, hFarDistToPursuePlayer, 2000.0f);
    NEBULA_TUNABLE(f32, hCommonGravity, 0.5f);
    NEBULA_TUNABLE(f32, hBlowGravity, 1.0f);
    NEBULA_TUNABLE(f32, hOverturnGravity, 2.0f);
    NEBULA_TUNABLE(f32, hOverturnBoundGravity, 1.0f);
    NEBULA_TUNABLE(f32, hRecoverGravity, 1.0f);
    NEBULA_TUNABLE(f32, hWallHitEndGravity, 2.0f);
    NEBULA_TUNABLE(f32, hTrampleJumpVelV, 5.0f);
    NEBULA_TUNABLE(f32, hBecomeAngryJumpVelV, 5.0f);
    NEBULA_TUNABLE(f32, hOverturnJumpVelV, 25.0f);
    NEBULA_TUNABLE(f32, hOverturnBoundJumpVelV, 15.0f);
    NEBULA_TUNABLE(f32, hRecoverJumpVelV, 17.0f);
    NEBULA_TUNABLE(f32, hWallHitEndJumpVelV, 20.0f);
    const s32 hAngryPursueTime = NebulaFrameClock::stepsOfSeconds(5);
    const s32 hOverturnTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(f32, hWalkSpeed, 4.0f);
    NEBULA_TUNABLE(f32, hAngrySpeed, 13.0f);
    NEBULA_TUNABLE(s32, hWalkStep, 8);
    NEBULA_TUNABLE(f32, hWalkAnimRate, 1.0f);
    NEBULA_TUNABLE(f32, hAngryAnimRate, 3.0f);
    NEBULA_TUNABLE(s32, hBecomeAngryInterval, 10);
    NEBULA_TUNABLE(s32, hPunchStopSceneTime, 4);
    NEBULA_TUNABLE(s32, hSpinInterval, 3);
    NEBULA_TUNABLE(s32, hOverturnCanTrampleDeathTime, 50);
    const s32 hRecoverCanTrampleDeathTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, hRecoverInit, 67);
    NEBULA_TUNABLE(s32, hRecoverInterval, 3);
    NEBULA_TUNABLE(s32, hRecoverAnimBegin, 55);
    const s32 hHipDroppedTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, hNearHipDropReactionDist, 1000.0f);
    NEBULA_TUNABLE(s32, hOverturnBoundInterval, 2);
    NEBULA_TUNABLE(s32, hWallHitInterval, 3);
    NEBULA_TUNABLE(f32, hHitWallCheck, -0.9f);
    NEBULA_TUNABLE(f32, hInitBlowRadian, 0.2f);
    const s32 hBlowTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, hBlowVelH, 30.0f);
    NEBULA_TUNABLE(f32, hBlowVelV, 20.0f);
    NEBULA_TUNABLE(s32, hBindMinStep, 10);
    NEBULA_TUNABLE(f32, hPushRate, 0.25f);
    NEBULA_TUNABLE(f32, hCollideRange, 2.0f);
    const s32 hRecoverSteamSeTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, hRecoverGetupSeTime, 63);
    NEBULA_TUNABLE(s32, hOverturnBoundSeTime, 29);
    NEBULA_TUNABLE(f32, hAngryEndJumpVelV, 0.0f);
    NEBULA_TUNABLE(s32, hTrampleInit, 0);
    NEBULA_TUNABLE(s32, hTrampleInterval, 0);
    NEBULA_TUNABLE(s32, hTrampleAnimBegin, 0);
    NEBULA_TUNABLE(s32, hSpinInit, 0);
    NEBULA_TUNABLE(s32, hOverturnAnimBegin, 0);
    NEBULA_TUNABLE(s32, hHipDroppedInit, 0);
    NEBULA_TUNABLE(s32, hHipDroppedInterval, 0);
    NEBULA_TUNABLE(s32, hOverturnBoundInit, 0);
    NEBULA_TUNABLE(s32, hOverturnBoundAnimBegin, 0);
};  // namespace

namespace NrvHanachan {
    NEW_NERVE(HanachanPartsNrvWalk, HanachanParts, Walk);
    NEW_NERVE_ONEND(HanachanPartsNrvBecomeAngry, HanachanParts, BecomeAngry, BecomeAngry);
    NEW_NERVE_ONEND(HanachanPartsNrvAngryPursue, HanachanParts, AngryPursue, AngryPursue);
    NEW_NERVE(HanachanPartsNrvAngryEnd, HanachanParts, AngryEnd);
    NEW_NERVE(HanachanPartsNrvWallHitEnd, HanachanParts, WallHitEnd);
    NEW_NERVE(HanachanPartsNrvTrample, HanachanParts, Trample);
    NEW_NERVE(HanachanPartsNrvOverturn, HanachanParts, Overturn);
    NEW_NERVE(HanachanPartsNrvOverturnBound, HanachanParts, OverturnBound);
    NEW_NERVE(HanachanPartsNrvOverturnWait, HanachanParts, OverturnWait);
    NEW_NERVE_ONEND(HanachanPartsNrvRecover, HanachanParts, Recover, Recover);
    NEW_NERVE(HanachanPartsNrvHipDropped, HanachanParts, HipDropped);
    NEW_NERVE(HanachanPartsNrvOverturnHipDropped, HanachanParts, HipDropped);
    NEW_NERVE(HanachanPartsNrvBlow, HanachanParts, Blow);
    NEW_NERVE_ONEND(HanachanPartsNrvStarPointerBind, HanachanParts, StarPointerBind, StarPointerBind);

    NEW_NERVE(HanachanNrvHanachanWalk, Hanachan, Walk);
    NEW_NERVE(HanachanNrvHanachanBecomeAngry, Hanachan, BecomeAngry);
    NEW_NERVE(HanachanNrvHanachanAngryPursue, Hanachan, AngryPursue);
    NEW_NERVE(HanachanNrvHanachanAngryEnd, Hanachan, AngryEnd);
    NEW_NERVE(HanachanNrvHanachanWallHitEnd, Hanachan, WallHitEnd);
    NEW_NERVE(HanachanNrvHanachanTrample, Hanachan, Trample);
    NEW_NERVE(HanachanNrvHanachanOverturn, Hanachan, Overturn);
    NEW_NERVE(HanachanNrvHanachanOverturnBound, Hanachan, OverturnBound);
    NEW_NERVE(HanachanNrvHanachanOverturnWait, Hanachan, OverturnWait);
    NEW_NERVE(HanachanNrvHanachanRecover, Hanachan, Recover);
    NEW_NERVE(HanachanNrvHanachanHipDropped, Hanachan, HipDropped);
    NEW_NERVE(HanachanNrvHanachanOverturnHipDropped, Hanachan, HipDropped);
    NEW_NERVE(HanachanNrvHanachanBlow, Hanachan, Blow);
    NEW_NERVE_ONEND(HanachanNrvHanachanStarPointerBind, Hanachan, StarPointerBind, StarPointerBind);
    NEW_NERVE(HanachanNrvHanachanStarPointerBindEnd, Hanachan, WallHitEnd);
    NEW_NERVE_ONEND(HanachanNrvHanachanStarPointerBindOverturn, Hanachan, StarPointerBind, StarPointerBind);
    NEW_NERVE(HanachanNrvHanachanStarPointerBindEndOverturn, Hanachan, WallHitEnd);
};  // namespace NrvHanachan

HanachanParts::HanachanParts(Hanachan* pHost, s32 partsIndex, const char* pName, const char* pModelName)
    : LiveActor(pName), mHost(pHost), mPushVelocity(0.0f), mFallVelocity(0.0f), mPartsType(PartsType_Body), mRotationQuat(0.0f, 0.0f, 0.0f, 1.0f),
      mPartsIndex(partsIndex), mActionStartStep(-1), mIsLanded() {
    lumacore::setClass(this, lumacore::kClassId< HanachanParts >);
    initModelManagerWithAnm(pModelName, nullptr, false);

    if (MR::isEqualString(pModelName, "HanachanBody")) {
        mPartsType = PartsType_Body;
    } else if (MR::isEqualString(pModelName, "HanachanBodyS")) {
        mPartsType = PartsType_BodyS;
    } else if (MR::isEqualString(pModelName, "HanachanHead")) {
        mPartsType = PartsType_Head;
    }
}

void HanachanParts::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_MapObjDecoration, MR::DrawBufferType_Enemy, MR::DrawType_None);
    initNerve(GET_NERVE(Hanachan, HanachanPartsNrvWalk));
    initHitSensor(1);

    if (mPartsType == PartsType_Body) {
        MR::addHitSensorEnemy(this, "body", 32, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    } else if (mPartsType == PartsType_BodyS) {
        MR::addHitSensorEnemy(this, "body", 32, cNum_init_f3, Vec3f(0.0f, cNum_init_f4, 0.0f));
    } else if (mPartsType == PartsType_Head) {
        MR::addHitSensorEnemy(this, "body", 32, cNum_init_f5, Vec3f(0.0f, cNum_init_f6, 0.0f));
    }

    initBinder(cNum_init_f7, cNum_init_f8, 0);

    initEffectKeeper(1, mPartsType == PartsType_Head ? "HanachanHead" : "HanachanBody");
    MR::addEffect(this, "PointerTouch");
    MR::initLightCtrl(this);
    initSound(2, false);
    MR::invalidateClipping(this);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, cNum_init_f9);
    MR::initStarPointerTarget(this, cNum_init_f10, Vec3f(0.0f, cNum_init_f11, 0.0f));
    MR::startBrk(this, "Normal");

    if (mPartsType == PartsType_Head) {
        MR::startBva(this, "normal");
    }
}

void HanachanParts::kill() {
    LiveActor::kill();

    MR::emitEffect(this, mPartsType == PartsType_Head ? "HeadDeath" : "BodyDeath");

    if (mPartsType == PartsType_Body || mPartsType == PartsType_Head) {
        MR::appearStarPiece(mHost, mPosition, 2, cNum_kill_f1, cNum_kill_f2, false);
        MR::startSound(mHost, "SE_OJ_STAR_PIECE_BURST");
    }
}

const Vec3f* HanachanParts::getCommonGravity() const {
    if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanTrample)) || mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanBecomeAngry)) ||
        mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturn)) || mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnBound)) ||
        mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanRecover))) {
        return &mHost->mBodyParts[2]->mGravity;
    }

    return &mGravity;
}

void HanachanParts::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (!MR::isPlayerDamaging()) {
            if (isNerve(GET_NERVE(Hanachan, HanachanPartsNrvAngryPursue)) && MR::isOnGroundPlayer()) {
                if (MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
                    mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWallHitEnd));
                    return;
                }

                MR::sendMsgPush(pReceiver, pSender);
            } else if (isNerve(GET_NERVE(Hanachan, HanachanPartsNrvWalk)) && MR::isOnGroundPlayer()) {
                if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                    return;
                }

                MR::sendMsgPush(pReceiver, pSender);
            } else if (!isNerve(GET_NERVE(Hanachan, HanachanPartsNrvHipDropped)) &&
                       !isNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnHipDropped))) {
                MR::sendMsgPush(pReceiver, pSender);
            }
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else if (MR::isSensorEnemy(pReceiver)) {
        if (!mHost->isOwnSensor(pReceiver) &&
            (isNerve(GET_NERVE(Hanachan, HanachanPartsNrvBecomeAngry)) || isNerve(GET_NERVE(Hanachan, HanachanPartsNrvAngryPursue))) &&
            MR::sendMsgToEnemyAttackBlow(pReceiver, pSender) && mPartsType == PartsType_Head) {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWallHitEnd));
            return;
        }

        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool HanachanParts::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgInvincibleAttack(msg)) {
        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanBlow)) || mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanHipDropped))) {
            return false;
        }

        mHost->setNerveBlow(pSender->mPosition);
        return true;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait)) || mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk))) {
            return false;
        }

        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait))) {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnBound));
            mHost->mAttackPos = pSender->mPosition;
            return true;
        }

        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk))) {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindEnd));
            mHost->mAttackPos = pSender->mPosition;
            return true;
        }

        return false;
    }

    if (isNerve(GET_NERVE(Hanachan, HanachanPartsNrvHipDropped)) || isNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnHipDropped))) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        if (!mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanHipDropped)) &&
            !mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnHipDropped))) {
            if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturn)) ||
                mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnBound)) ||
                mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait)) ||
                mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanRecover))) {
                mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnHipDropped));
            } else {
                mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanHipDropped));
            }

            mHost->mAttackPos = pSender->mPosition;
            return true;
        }

        return false;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        MR::startSound(this, "SE_EM_HANACHAN_TRAMPLE");

        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanAngryPursue)) || mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanAngryEnd)) ||
            mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWallHitEnd)) || mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanTrample)) ||
            mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanBecomeAngry))) {
            return true;
        }

        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait)) ||
            (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturn)) && MR::isGreaterStep(mHost, ::hOverturnCanTrampleDeathTime)) ||
            mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnBound)) ||
            (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanRecover)) && MR::isLessStep(mHost, ::hRecoverCanTrampleDeathTime)) ||
            mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindOverturn)) ||
            mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindEndOverturn))) {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnHipDropped));
            mHost->mAttackPos = pSender->mPosition;
            return true;
        }

        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk))) {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanTrample));
            mHost->mAttackPos = pSender->mPosition;
            return true;
        }

        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturn)) ||
            mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnBound))) {
            return false;
        }

        MR::stopScene(::hPunchStopSceneTime);

        if (mHost->isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait))) {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnBound));
        } else {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturn));
        }

        mHost->mAttackPos = pSender->mPosition;
        return true;
    }

    return false;
}

bool HanachanParts::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return true;
}

bool HanachanParts::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pSender)) {
        return false;
    }

    Vec3f dir = pReceiver->mPosition - pSender->mPosition;

    f32 depth;

    if (!mHost->isOwnSensor(pSender)) {
        f32 senderRadius = pSender->mRadius;
        f32 receiverRadius = pReceiver->mRadius;
        depth = (receiverRadius + senderRadius) - dir.length();
    } else {
        f32 senderRadius = pSender->mRadius;
        f32 receiverRadius = pReceiver->mRadius;
        depth = (receiverRadius / cNum_receiveMsgPush_f1 + senderRadius / cNum_receiveMsgPush_f2) - dir.length();

        if (depth < 0.0f) {
            return false;
        }
    }

    dir.setLength(::hPushRate * depth);
    mPushVelocity.add(dir);
    return true;
}

void HanachanParts::calcAndSetBaseMtx() {
    Quat4f rot;
    rot = mRotationQuat;

    Pos3f mtx;
    mtx.makeQuat(rot);
    mtx.setTrans(mPosition);

    MR::setBaseTRMtx(this, mtx);
    MR::setBaseScale(this, mHost->mScaleController->mScale * mScale);
}

void HanachanParts::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Walk");
        MR::setBckFrameAndStop(this, ::hWalkStep * mPartsIndex);
        MR::setBckRate(this, ::hWalkAnimRate);
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        mFallVelocity.zero();
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hCommonGravity);
        mFallVelocity.mult(cNum_exeWalk_f1);
    }
}

void HanachanParts::exeTrample() {
    if (MR::isFirstStep(this)) {
        mIsLanded = false;
    }

    if (MR::isStep(this, mActionStartStep + ::hTrampleAnimBegin)) {
        MR::startAction(this, "AngryStart");
    }

    if (MR::isStep(this, mActionStartStep)) {
        mFallVelocity.set(*getCommonGravity() * -::hTrampleJumpVelV);
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        if (!mIsLanded && MR::isGreaterStep(this, mActionStartStep)) {
            mIsLanded = true;
            changeHeadAngry();
            MR::startAction(this, "Walk");

            MR::setBckFrameAndStop(this, mPartsIndex * ::hWalkStep);
            MR::setBckRate(this, ::hAngryAnimRate);
        }

        mFallVelocity.zero();
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hCommonGravity);
        mFallVelocity.mult(cNum_exeTrample_f1);
    }
}

void HanachanParts::exeBecomeAngry() {
    if (MR::isFirstStep(this)) {
        mFallVelocity.set(*getCommonGravity() * -::hBecomeAngryJumpVelV);
        mActionStartStep = 0;
        mIsLanded = false;
        MR::startBrk(this, "Anger");
        MR::invalidateClipping(mHost);
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        if (!mIsLanded && MR::isGreaterStep(this, mActionStartStep)) {
            mIsLanded = true;
        }

        mFallVelocity.zero();
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hCommonGravity);
        mFallVelocity.mult(cNum_exeBecomeAngry_f1);
    }
}

void HanachanParts::endBecomeAngry() {
    changeHeadCalmDown();
}

void HanachanParts::exeAngryPursue() {
    if (MR::isFirstStep(this)) {
        changeHeadAngry();
    }

    bool isWallHit = isHeadHitWall() && MR::isBindedWallOfMap(this);

    if (isWallHit) {
        mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWallHitEnd));
    } else {
        bool isMoveLimitHit = isHeadHitWall() && MR::isBindedWallOfMoveLimit(this);

        if (isMoveLimitHit) {
            mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanAngryEnd));
        } else {
            mVelocity.set(mFallVelocity);
            mVelocity.add(mPushVelocity);

            if (MR::isOnGround(this)) {
                mFallVelocity.zero();
            } else {
                mFallVelocity.add(mGravity * ::hCommonGravity);
                mFallVelocity.mult(cNum_exeAngryPursue_f1);
            }
        }
    }
}

void HanachanParts::endAngryPursue() {
    changeHeadCalmDown();
}

void HanachanParts::exeAngryEnd() {
    if (MR::isFirstStep(this)) {
        mIsLanded = false;
    }

    if (MR::isStep(this, mActionStartStep)) {
        MR::startAction(this, "AngryEnd");
        MR::startBrk(this, "Normal");
        MR::validateClipping(mHost);
    }

    if (MR::isStep(this, mActionStartStep)) {
        mFallVelocity.set(*getCommonGravity() * -::hAngryEndJumpVelV);
    }

    bool isWallHit = isHeadHitWall() && MR::isBindedWallOfMap(this);

    if (isWallHit) {
        mHost->setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWallHitEnd));
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        if (!mIsLanded && MR::isGreaterStep(this, mActionStartStep)) {
            mIsLanded = true;
        }

        mFallVelocity.zero();
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hCommonGravity);
        mFallVelocity.mult(cNum_exeAngryEnd_f1);
    }
}

void HanachanParts::exeWallHitEnd() {
    if (MR::isFirstStep(this)) {
        mIsLanded = false;
    }

    if (MR::isStep(this, mActionStartStep)) {
        MR::startBrk(this, "Normal");
        MR::validateClipping(mHost);
        mFallVelocity.set(*getCommonGravity() * -::hWallHitEndJumpVelV);
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        if (!mIsLanded && MR::isGreaterStep(this, mActionStartStep)) {
            mIsLanded = true;
        }

        mFallVelocity.zero();
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hWallHitEndGravity);
        mFallVelocity.mult(cNum_exeWallHitEnd_f1);
    }
}

void HanachanParts::exeOverturn() {
    if (MR::isStep(this, mActionStartStep + ::hOverturnAnimBegin)) {
        MR::startAction(this, "Overturn");
        MR::startBrk(this, "Normal");
        MR::validateClipping(mHost);
        changeHeadCalmDown();
    }

    if (MR::isStep(this, mActionStartStep)) {
        mFallVelocity.set(*getCommonGravity() * -::hOverturnJumpVelV);
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        mFallVelocity.zero();

        if (MR::isGreaterStep(this, mActionStartStep)) {
            setNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnWait));
        }
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hOverturnGravity);
        mFallVelocity.mult(cNum_exeOverturn_f1);
    }
}

void HanachanParts::exeOverturnBound() {
    if (MR::isStep(this, mActionStartStep + ::hOverturnBoundAnimBegin)) {
        MR::startAction(this, "OverturnBound");
    }

    if (MR::isStep(this, mActionStartStep)) {
        mFallVelocity.set(*getCommonGravity() * -::hOverturnBoundJumpVelV);
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        mFallVelocity.zero();

        if (MR::isGreaterStep(this, mActionStartStep)) {
            setNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnWait));
        }
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hOverturnBoundGravity);
        mFallVelocity.mult(cNum_exeOverturnBound_f1);
    }
}

void HanachanParts::exeOverturnWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "OverturnLand");
        mIsLanded = false;
    }

    if (!mIsLanded && MR::isBckStopped(this)) {
        MR::startAction(this, "OverturnWait");
        s32 frame = mPartsIndex * ::hWalkStep;
        MR::setBckFrameAndStop(this, frame);
        MR::setBckRate(this, ::hWalkAnimRate);
        mIsLanded = true;
    }

    mVelocity.zero();
    mVelocity.add(mPushVelocity);
}

void HanachanParts::exeRecover() {
    if (MR::isFirstStep(this)) {
        mIsLanded = false;
    }

    if (MR::isStep(this, mActionStartStep - ::hRecoverAnimBegin)) {
        MR::startBrk(this, "Anger");
        MR::invalidateClipping(mHost);
        MR::startAction(this, "Recover");
    }

    if (MR::isStep(this, mActionStartStep)) {
        mFallVelocity.set(*getCommonGravity() * -::hRecoverJumpVelV);
    }

    if (MR::isGreaterStep(this, mActionStartStep - 50) && mPartsType == PartsType_Head) {
        MR::startBva(this, "Anger");
    }

    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isOnGround(this)) {
        if (mActionStartStep == 0) {
            if (MR::isBckOneTimeAndStopped(this)) {
                mIsLanded = true;
                MR::startAction(this, "Walk");
                MR::setBckRate(this, ::hAngryAnimRate);
            }
        } else if (MR::isGreaterStep(this, mActionStartStep)) {
            MR::startAction(this, "RecoverLand");
            mActionStartStep = 0;
        }

        mFallVelocity.zero();
    } else {
        mFallVelocity.add(*getCommonGravity() * ::hRecoverGravity);
        mFallVelocity.mult(cNum_exeRecover_f1);
    }
}

void HanachanParts::endRecover() {
    changeHeadCalmDown();
}

void HanachanParts::exeHipDropped() {
    mVelocity.set(mFallVelocity);
    mVelocity.add(mPushVelocity);

    if (MR::isFirstStep(this)) {
        MR::forceBindOnGround(this, 0.0f, 1.0f);
        mIsLanded = false;
    }

    if (MR::isStep(this, mActionStartStep)) {
        s32 nearestId = mHost->calcNearestInfectionId();
        bool isOverturn = isNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnHipDropped));

        if (mPartsIndex == nearestId) {
            MR::startAction(this, isOverturn ? "OverturnPress" : "Press");
        } else if (nearestId == 1 && mPartsType == PartsType_Head) {
            MR::startAction(this, isOverturn ? "OverturnPress" : "Press");
        } else {
            MR::stopBck(this);
        }
    }

    if (MR::isOnGround(this)) {
        if (MR::isGreaterStep(this, mActionStartStep)) {
            if (mPartsIndex == mHost->calcNearestInfectionId()) {
                MR::isBckStopped(this);
                mIsLanded = true;
            } else {
                mIsLanded = true;
            }
        }

        mFallVelocity.zero();
    } else {
        mFallVelocity.add(*getCommonGravity() * cNum_exeHipDropped_f1);
        mFallVelocity.mult(cNum_exeHipDropped_f2);
    }
}

void HanachanParts::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Rotate");
        mVelocity.set(mPushVelocity);

        if (mPartsType == PartsType_Head) {
            MR::invalidateExCollisionParts(this);
        }
    }

    mVelocity.add(mGravity * ::hBlowGravity);

    if ((MR::isGreaterStep(this, cStep_exeBlow_1) && MR::isBinded(this)) || MR::isGreaterStep(this, ::hBlowTime)) {
        kill();
    }
}

void HanachanParts::exeStarPointerBind() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        MR::stopBck(this);
        mVelocity.zero();
        MR::emitEffect(this, "PointerTouch");
    }

    MR::startPointerFreezeLevelSound(this);
}

void HanachanParts::endStarPointerBind() {
    MR::deleteEffect(this, "PointerTouch");
    MR::setBckRate(this, cNum_endStarPointerBind_setBckRate_1);
}

bool HanachanParts::isHeadHitWall() {
    if (mPartsType == PartsType_Head && MR::isBindedWall(this)) {
        Vec3f wallNormal = *MR::getWallNormal(this);

        if (wallNormal.dot(mHost->mFrontDir) <= ::hHitWallCheck) {
            return true;
        }
    }

    return false;
}

bool HanachanParts::isLandedInNerve(const Nerve* pNerve) {
    return isNerve(pNerve) && mIsLanded;
}

bool HanachanParts::isHipDroppedLanded() {
    return (isNerve(GET_NERVE(Hanachan, HanachanPartsNrvHipDropped)) || isNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnHipDropped))) &&
           mIsLanded;
}

void HanachanParts::changeHeadAngry() {
    if (mPartsType == PartsType_Head) {
        MR::startBva(this, "Anger");
        MR::emitEffect(this, "AngrySmoke");
    }
}

void HanachanParts::changeHeadCalmDown() {
    if (mPartsType == PartsType_Head) {
        MR::startBva(this, "Normal");
        MR::deleteEffect(this, "AngrySmoke");
    }
}

Hanachan::Hanachan(const char* pName) : LiveActor(pName), mAttackPos(0, 0, 0), mFrontDir(0, 0, 1), mIsChasePlayer(true), mScaleController() {
    lumacore::setClass(this, lumacore::kClassId< Hanachan >);
}

void Hanachan::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    // Without a rail there is nowhere to walk: it stays dead.
    if (!MR::isConnectedWithRail(rIter)) {
        makeActorDead();
        return;
    }

    initRailRider(rIter);

    s32 noChase = 0;
    MR::getPlacementArg0NoInit(rIter, &noChase);
    mIsChasePlayer = noChase != 1;

    MR::useStageSwitchWriteDead(this, rIter);
    MR::connectToSceneEnemyMovement(this);
    initNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk));
    initSound(6, false);
    MR::validateClipping(this);
    mScaleController = new AnimScaleController(nullptr);
    MR::declareStarPiece(this, 6);
    appear();

    mBodyParts[0] = new HanachanParts(this, 0, "頭パーツ", "HanachanHead");
    mBodyParts[1] = new HanachanParts(this, 1, "体パーツ", "HanachanBodyS");
    mBodyParts[2] = new HanachanParts(this, 2, "体パーツ", "HanachanBody");
    mBodyParts[3] = new HanachanParts(this, 3, "体パーツ", "HanachanBodyS");
    mBodyParts[4] = new HanachanParts(this, 4, "体パーツ", "HanachanBody");

    mBodyParts[0]->initWithoutIter();
    mBodyParts[1]->initWithoutIter();
    mBodyParts[2]->initWithoutIter();
    mBodyParts[3]->initWithoutIter();
    mBodyParts[4]->initWithoutIter();

    MR::moveCoordToNearestPos(this, mPosition);
    MR::moveTransToOtherActorRailPos(mBodyParts[0], this);
    MR::reverseRailDirection(this);

    MR::moveCoord(this, mBodyParts[0]->getSensor("body")->getRadius() / cNum_init_f12 + mBodyParts[1]->getSensor("body")->getRadius() / cNum_init_f13);
    MR::moveTransToOtherActorRailPos(mBodyParts[1], this);
    MR::moveCoord(this, mBodyParts[1]->getSensor("body")->getRadius() / cNum_init_f14 + mBodyParts[2]->getSensor("body")->getRadius() / cNum_init_f15);
    MR::moveTransToOtherActorRailPos(mBodyParts[2], this);
    MR::moveCoord(this, mBodyParts[2]->getSensor("body")->getRadius() / cNum_init_f16 + mBodyParts[3]->getSensor("body")->getRadius() / cNum_init_f17);
    MR::moveTransToOtherActorRailPos(mBodyParts[3], this);
    MR::moveCoord(this, mBodyParts[3]->getSensor("body")->getRadius() / cNum_init_f18 + mBodyParts[4]->getSensor("body")->getRadius() / cNum_init_f19);
    MR::moveTransToOtherActorRailPos(mBodyParts[4], this);
    MR::reverseRailDirection(this);

    mBodyParts[0]->appear();
    mBodyParts[1]->appear();
    mBodyParts[2]->appear();
    mBodyParts[3]->appear();
    mBodyParts[4]->appear();
}

void Hanachan::initAfterPlacement() {
    if (MR::isDead(this)) {
        return;
    }

    MR::trySetMoveLimitCollision(mBodyParts[0]);
}

void Hanachan::kill() {
    MR::startSound(this, "SE_EM_EXPLODE_S");
    LiveActor::kill();
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void Hanachan::startClipped() {
    LiveActor::startClipped();

    for (HanachanParts** current = mBodyParts.begin(); current < mBodyParts.end(); current++) {
        (*current)->startClipped();
    }
}

void Hanachan::endClipped() {
    LiveActor::endClipped();

    for (HanachanParts** current = mBodyParts.begin(); current < mBodyParts.end(); current++) {
        (*current)->endClipped();
    }
}

void Hanachan::control() {
    mScaleController->updateNerve();

    for (HanachanParts** current = mBodyParts.begin(); current < mBodyParts.end(); current++) {
        if (!MR::isDead(*current)) {
            (*current)->movement();
            (*current)->mPushVelocity.zero();
        }
    }

    if ((isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk)) || isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanAngryPursue)) ||
         isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait))) &&
        isStarPointerPointing()) {
        if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait))) {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindOverturn));
        } else {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBind));
        }
    }

    mPosition.set(mBodyParts[2]->mPosition);

    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk))) {
            MR::validateClipping(this);
        } else {
            MR::invalidateClipping(this);
        }
    }
}

void Hanachan::exeWalk() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvWalk));
    }

    moveHeadAlongRail(::hWalkSpeed);
    moveBodyAlongHead();
    applyPlayerHipDropReaction();
}

void Hanachan::exeTrample() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvTrample));
        setDelayAllPartsAtId(calcNearestInfectionId(), ::hTrampleInit, ::hTrampleInterval);
        MR::startSound(this, "SE_EV_HANACHAN_TRAMPLE");
    }

    moveBodyAlongHead();

    if (mBodyParts[0]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvTrample)) &&
        mBodyParts[4]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvTrample))) {
        setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanBecomeAngry));
    }
}

void Hanachan::exeBecomeAngry() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvBecomeAngry));
        setDelayAllPartsAtId(0, 0, ::hBecomeAngryInterval);
    }

    MR::startLevelSound(this, "SE_EM_LV_HANACHAN_STEAM");
    MR::startLevelSound(this, "SE_EM_LV_HANACHAN_PRE_ANGRY");
    moveBodyAlongHead();

    if (mBodyParts[0]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvBecomeAngry)) &&
        mBodyParts[4]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvBecomeAngry))) {
        setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanAngryPursue));
    }
}

void Hanachan::exeAngryPursue() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvAngryPursue));
    }

    MR::startLevelSound(this, "SE_EM_LV_HANACHAN_WHISTLE");
    MR::startLevelSound(this, "SE_EM_LV_HANACHAN_MOVE_ANGRY");

    if (mIsChasePlayer) {
        moveHeadToPlayer(::hAngrySpeed, cNum_exeAngryPursue_f2);
    } else {
        moveHeadAlongRail(::hAngrySpeed);
    }

    moveBodyAlongHead();
    applyPlayerHipDropReaction();

    if (MR::isGreaterStep(this, ::hAngryPursueTime)) {
        setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanAngryEnd));
    }
}

void Hanachan::exeAngryEnd() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvAngryEnd));
        setDelayAllPartsAtId(4, 0, 0);
        MR::startSound(this, "SE_EV_HANACHAN_COOLDOWN");
    }

    MR::startLevelSound(this, "SE_EM_LV_HANACHAN_BRAKE");

    f32 speed = 1.0f - NebulaFrameClock::secondsOfSteps(getNerveStep());
    speed *= ::hAngrySpeed;
    mBodyParts[0]->mRotationQuat.getZDir(mFrontDir);
    mBodyParts[0]->mPushVelocity.add(mFrontDir * speed);

    HanachanParts* pHead = mBodyParts[0];
    MR::blendQuatUpFront(&pHead->mRotationQuat, -pHead->mGravity, mFrontDir, cNum_exeAngryEnd_f2, cNum_exeAngryEnd_f3);

    moveBodyAlongHead();
    applyPlayerHipDropReaction();

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        if (mBodyParts[0]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvAngryEnd)) &&
            mBodyParts[4]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvAngryEnd))) {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk));
        }
    }
}

void Hanachan::exeWallHitEnd() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EV_HANACHAN_COOLDOWN");
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvWallHitEnd));

        if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindEnd)) ||
            isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindEndOverturn))) {
            setDelayAllPartsAtId(calcNearestInfectionId(), 0, ::hWallHitInterval);
        } else if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWallHitEnd))) {
            setDelayAllPartsAtId(0, 0, ::hWallHitInterval);
            MR::emitEffect(mBodyParts[0], "WallHit");
        }
    }

    if (mBodyParts[0]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvWallHitEnd)) &&
        mBodyParts[4]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvWallHitEnd))) {
        if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindEndOverturn))) {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait));
        } else {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanWalk));
        }
    }
}

void Hanachan::exeOverturn() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvOverturn));
        setDelayAllPartsAtId(calcNearestInfectionId(), ::hSpinInit, ::hSpinInterval);
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_EV_HANACHAN_OVERTURN");
    }

    if (MR::isStep(this, ::hOverturnBoundSeTime)) {
        MR::startSound(this, "SE_EM_HANACHAN_BOUND");
    }

    moveBodyAlongHead();

    if (mBodyParts[0]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnWait)) &&
        mBodyParts[4]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnWait))) {
        setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait));
    }
}

void Hanachan::exeOverturnWait() {
    if (MR::isGreaterStep(this, ::hOverturnTime)) {
        setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanRecover));
    } else {
        MR::startLevelSound(this, "SE_EM_LV_HANACHAN_SWOON");
        applyPlayerHipDropReaction();
        moveBodyAlongHead();
    }
}

void Hanachan::exeOverturnBound() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvOverturnBound));
        setDelayAllPartsAtId(calcNearestInfectionId(), ::hOverturnBoundInit, ::hOverturnBoundInterval);
        MR::startBlowHitSound(this);
    }

    if (MR::isStep(this, ::hOverturnBoundSeTime)) {
        MR::startSound(this, "SE_EM_HANACHAN_BOUND");
    }

    moveBodyAlongHead();

    if (mBodyParts[0]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnWait)) &&
        mBodyParts[4]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvOverturnWait))) {
        setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait));
    }
}

void Hanachan::exeRecover() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvRecover));
        setDelayAllPartsAtId(0, ::hRecoverInit, ::hRecoverInterval);
    }

    MR::startLevelSound(this, "SE_EM_LV_HANACHAN_PRE_ANGRY");

    if (MR::isGreaterEqualStep(this, ::hRecoverSteamSeTime)) {
        MR::startLevelSound(this, "SE_EM_LV_HANACHAN_STEAM");
    }

    if (MR::isStep(this, ::hRecoverGetupSeTime)) {
        MR::startSound(this, "SE_EM_HANACHAN_GETUP");
        MR::startSound(this, "SE_EV_HANACHAN_ANGRY");
    }

    moveBodyAlongHead();

    if (mBodyParts[0]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvRecover)) &&
        mBodyParts[4]->isLandedInNerve(GET_NERVE(Hanachan, HanachanPartsNrvRecover))) {
        setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanAngryPursue));
    }
}

void Hanachan::exeHipDropped() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnHipDropped))) {
            setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvOverturnHipDropped));
        } else {
            setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvHipDropped));
        }

        setDelayAllPartsAtId(calcNearestInfectionId(), ::hHipDroppedInit, ::hHipDroppedInterval);
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::startSound(this, "SE_EV_HANACHAN_DEAD");
        mScaleController->startHitReaction();
    }

    if (MR::isGreaterStep(this, ::hHipDroppedTime)) {
        if (mBodyParts[0]->isHipDroppedLanded() && mBodyParts[4]->isHipDroppedLanded()) {
            for (s32 i = 0; i < mBodyParts.size(); i++) {
                mBodyParts[i]->kill();
            }

            kill();
        }
    }
}

void Hanachan::exeBlow() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvBlow));
        MR::startBlowHitSound(this);
    }

    for (HanachanParts** current = mBodyParts.begin(); current < mBodyParts.end(); current++) {
        if (!MR::isDead(*current)) {
            return;
        }
    }

    kill();
}

void Hanachan::exeStarPointerBind() {
    if (MR::isFirstStep(this)) {
        setNerveAllParts(GET_NERVE(Hanachan, HanachanPartsNrvStarPointerBind));
        mScaleController->startPointerHitVibration();
    }

    if (MR::isGreaterStep(this, ::hBindMinStep) && !isStarPointerPointing()) {
        if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindOverturn))) {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindEndOverturn));
        } else {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanStarPointerBindEnd));
        }

        mAttackPos = *MR::getStarPointerWorldPosUsingDepth(0);
    }
}

void Hanachan::endStarPointerBind() {
    mScaleController->startAnim();
}

s32 Hanachan::calcNearestInfectionId() {
    f32 nearestDist = cNum_calcNearestInfectionId_f1;
    s32 nearestId = -1;

    for (s32 i = 0; i < mBodyParts.size(); i++) {
        f32 dist = mBodyParts[i]->getSensor("body")->mPosition.squared(mAttackPos);

        if (dist < nearestDist) {
            nearestDist = dist;
            nearestId = i;
        }
    }

    if (nearestId == -1) {
        return 0;
    }

    return nearestId;
}

bool Hanachan::isOwnSensor(HitSensor* pSensor) {
    for (HanachanParts** current = mBodyParts.begin(); current < mBodyParts.end(); current++) {
        if ((*current)->getSensor("body") == pSensor) {
            return true;
        }
    }

    return false;
}

void Hanachan::setNerveBlow(const Vec3f& rPos) {
    Vec3f dir = *getPosition() - rPos;
    MR::vecKillElement(dir, mGravity, &dir);
    MR::normalizeOrZero(&dir);

    const Vec3f& rGravity = mGravity;
    Vec3f side = dir.cross(rGravity);

    f32 angle = ::hInitBlowRadian;
    f32 numSegments = mBodyParts.size() > 1 ? static_cast< f32 >(mBodyParts.size() - 1) : 1.0f;
    f32 angleStep = (MR::pi() - ::hInitBlowRadian * cNum_setNerveBlow_f1) / numSegments;
    Vec3f blow;
    Vec3f up;

    Vec3f headDir = *mBodyParts[0]->getPosition() - rPos;

    if (headDir.dot(side) < 0.0f) {
        angleStep = -angleStep;
        angle = MR::pi() - angle;
    }

    for (HanachanParts** current = mBodyParts.begin(); current < mBodyParts.end(); current++) {
        blow = side * MR::cos(angle);
        blow.add(dir * MR::sin(angle));
        blow.setLength(::hBlowVelH);

        up.set((*current)->mGravity * -1.0f);
        up.scale(::hBlowVelV);
        (*current)->mPushVelocity.set(blow + up);

        angle += angleStep;
    }

    setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanBlow));
}

void Hanachan::applyPlayerHipDropReaction() {
    if (MR::isPlayerHipDropLand()) {
        mAttackPos.set(*MR::getPlayerCenterPos());

        f32 nearestDist = cNum_applyPlayerHipDropReaction_f1;

        for (s32 i = 0; i < mBodyParts.size(); i++) {
            f32 dist = MR::calcDistanceToPlayer(mBodyParts[i]);

            if (dist < nearestDist) {
                nearestDist = dist;
            }
        }

        if (::hNearHipDropReactionDist < nearestDist) {
            return;
        }

        if (isNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnWait))) {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturnBound));
        } else {
            setNerve(GET_NERVE(Hanachan, HanachanNrvHanachanOverturn));
        }
    }
}

void Hanachan::setNerveAllParts(const Nerve* pNerve) {
    for (s32 i = 0; i < mBodyParts.size(); i++) {
        mBodyParts[i]->setNerve(pNerve);
        mBodyParts[i]->mIsLanded = false;
    }
}

void Hanachan::setDelayAllPartsAtId(s32 id, s32 delay, s32 step) {
    s32 i;
    s32 currentDelay = delay;

    for (i = id; i >= 0; i--) {
        mBodyParts[i]->mActionStartStep = currentDelay;
        currentDelay += step;
    }

    s32 nextDelay = delay + step;

    for (i = id + 1; i < mBodyParts.size(); i++) {
        mBodyParts[i]->mActionStartStep = nextDelay;
        nextDelay += step;
    }
}

bool Hanachan::isStarPointerPointing() {
    for (s32 i = 0; i < mBodyParts.size(); i++) {
        if (MR::isStarPointerPointing2POnPressButton(mBodyParts[i], "弱", true, false)) {
            return true;
        }
    }

    return false;
}

void Hanachan::moveHeadAlongRail(f32 speed) {
    mBodyParts[0]->mRotationQuat.getZDir(mFrontDir);

    Vec3f railDir;
    f32 dist = MR::calcMovingDirectionAlongRail(this, &railDir, mBodyParts[0]->mPosition, cNum_moveHeadAlongRail_f1, false, nullptr);
    if (::hFarDistToPursuePlayer < dist) {
        moveHeadToPlayer(speed, cNum_moveHeadAlongRail_f2);
    } else {
        MR::vecKillElement(railDir, mBodyParts[0]->mGravity, &railDir);
        MR::normalizeOrZero(&railDir);

        if (!MR::isNearZero(railDir)) {
            MR::turnVecToVecRadian(&mFrontDir, mFrontDir, railDir, cNum_moveHeadAlongRail_f3, mBodyParts[0]->mGravity);
        }

        mBodyParts[0]->mPushVelocity.add(mFrontDir * speed);

        HanachanParts* pHead = mBodyParts[0];
        MR::blendQuatUpFront(&pHead->mRotationQuat, -pHead->mGravity, mFrontDir, cNum_moveHeadAlongRail_f4, cNum_moveHeadAlongRail_f5);
    }
}

void Hanachan::moveHeadToPlayer(f32 speed, f32 turnSpeed) {
    mBodyParts[0]->mRotationQuat.getZDir(mFrontDir);

    Vec3f playerDir;
    MR::calcVecToPlayerH(&playerDir, mBodyParts[0], nullptr);
    MR::turnVecToVecRadian(&mFrontDir, mFrontDir, playerDir, turnSpeed, mBodyParts[0]->mGravity);

    mBodyParts[0]->mPushVelocity.add(mFrontDir * speed);

    HanachanParts* pHead = mBodyParts[0];
    MR::blendQuatUpFront(&pHead->mRotationQuat, -pHead->mGravity, mFrontDir, cNum_moveHeadToPlayer_f1, cNum_moveHeadToPlayer_f2);
}

Vec3f Hanachan::calcSensorDirection(const HitSensor* pSensor, const HitSensor* pTarget) const {
    Vec3f dir = pTarget->mPosition - pSensor->mPosition;
    MR::normalizeOrZero(&dir);
    return dir;
}

void Hanachan::moveBodyAlongHead() {
    Vec3f* grav;
    for (s32 i = 1; i < mBodyParts.size(); i++) {
        Vec3f toPrev = mBodyParts[i - 1]->getSensor("body")->mPosition;
        toPrev.sub(mBodyParts[i]->getSensor("body")->mPosition);

        Vec3f push = -toPrev;
        push.setLength(::hCollideRange + mBodyParts[i - 1]->getSensor("body")->getRadius() / cNum_moveBodyAlongHead_f1 +
                       mBodyParts[i]->getSensor("body")->getRadius() / cNum_moveBodyAlongHead_f2);

        mBodyParts[i]->mPushVelocity.add(toPrev + push);

        grav = &mBodyParts[i]->mGravity;
        HitSensor* pTarget = mBodyParts[i - 1]->getSensor("body");
        HitSensor* pSensor = mBodyParts[i]->getSensor("body");
        Quat4f* pRotation = &mBodyParts[i]->mRotationQuat;

        MR::blendQuatUpFront(pRotation, -*grav, calcSensorDirection(pSensor, pTarget), cNum_moveBodyAlongHead_f3, cNum_moveBodyAlongHead_f4);
    }
}
