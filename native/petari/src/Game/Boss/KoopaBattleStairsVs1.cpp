#include "Game/Boss/KoopaBattleStairsVs1.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFireStairs.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Boss/KoopaSwitchKeeper.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/KoopaBattleMapStair.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeJumpToNextPosLoop_f1, "exeJumpToNextPosLoop.f1", 180.0f);

namespace {
    NEBULA_TUNABLE(s32, cAnimCameraInterpoleFrame, 60);
    NEBULA_TUNABLE(s32, cBgmStopFrame, 60);
    // Demo frames at which the far and the near demo stair are set alight.
    NEBULA_TUNABLE(s32, cDemoFarFireStep, 264);
    NEBULA_TUNABLE(s32, cDemoNearFireStep, 292);
    NEBULA_TUNABLE(s32, cStepToEmitFire, 15);
    NEBULA_TUNABLE(f32, cFireSpeed, 20.0f);
    NEBULA_TUNABLE(s32, cFireAttackStep, 33);
    const s32 cJumpTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, cJumpHeight, 1000.0f);
    NEBULA_TUNABLE(f32, cTurnSpeed, 2.0f);
    const char* const cLandingPosName[KoopaBattleStairsVs1::cLandingNum] = {
        "階段の戦い０（クッパ）",
        "階段の戦い１（クッパ）",
        "階段の戦い２（クッパ）",
    };
    const char* const cDemoCenterPosName = "デモ中心";

    // Landing 0 is where the battle starts, so a jump lands on 1, 2, then the demo centre.
    const char* getJumpLandPosName(s32 landingIndex) {
        return landingIndex == 1 || landingIndex == 2 ? cLandingPosName[landingIndex] : cDemoCenterPosName;
    }

    // Stairs of level 1 and 2 are fired from those landings; every other level from landing 0.
    s32 getFireLanding(const KoopaBattleMapStair* pStair) {
        return pStair->mLevel == 1 || pStair->mLevel == 2 ? pStair->mLevel : 0;
    }

    KoopaFireStairs* emitFireStairs(Koopa* pKoopa, const KoopaBattleMapStair* pStair) {
        if (pStair == nullptr) {
            return nullptr;
        }

        Vec3f jointPos(0.0f, 0.0f, 0.0f);
        MR::copyJointPos(pKoopa, "Tongue2", &jointPos);
        return KoopaFunction::emitFireStairsToTarget(pKoopa, pStair, jointPos, false);
    }

    KoopaFireStairs* emitDemoFireStairs(Koopa* pKoopa, KoopaBattleMapStair* pStair) {
        // A level without this demo stair, or a used-up fire pool, skips that part of the demo.
        KoopaFireStairs* pFire = emitFireStairs(pKoopa, pStair);
        MR::startSound(pKoopa, "SE_OJ_KOOPA_BULLET_SHOOT");

        if (pFire != nullptr) {
            MR::requestMovementOn(pFire);
        }

        if (pStair != nullptr) {
            MR::requestMovementOn(pStair);
        }

        return pFire;
    }
}  // namespace

namespace NrvKoopaBattleStairsVs1 {
    NEW_NERVE(KoopaBattleStairsVs1NrvWaitDemo, KoopaBattleStairsVs1, WaitDemo);
    NEW_NERVE(KoopaBattleStairsVs1NrvDemo, KoopaBattleStairsVs1, Demo);
    NEW_NERVE(KoopaBattleStairsVs1NrvWait, KoopaBattleStairsVs1, Wait);
    NEW_NERVE(KoopaBattleStairsVs1NrvAttackFire, KoopaBattleStairsVs1, AttackFire);
    NEW_NERVE(KoopaBattleStairsVs1NrvJumpToNextPosStart, KoopaBattleStairsVs1, JumpToNextPosStart);
    NEW_NERVE(KoopaBattleStairsVs1NrvJumpToNextPosLoop, KoopaBattleStairsVs1, JumpToNextPosLoop);
    NEW_NERVE(KoopaBattleStairsVs1NrvJumpToNextPosEnd, KoopaBattleStairsVs1, JumpToNextPosEnd);
}  // namespace NrvKoopaBattleStairsVs1

// calcFireStartPos moves Koopa to each landing in turn; he is left on landing 2 until the demo places him.
KoopaBattleStairsVs1::KoopaBattleStairsVs1(Koopa* pKoopa)
    : KoopaBattleStairsBase(pKoopa), mFarBattleMapStair(), mNearBattleMapStair(), mFireStair(), mIsJumpRequested(),
      mStairNum(), mAttackCount(), mLandingIndex(), mOldPosition(0.0f, 0.0f, 0.0f), mNewPosition(0.0f, 0.0f, 0.0f), mNewDirection(0.0f, 0.0f, 1.0f) {
    initNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvWaitDemo));

    for (s32 i = 0; i < cLandingNum; i++) {
        mFireStartPos[i].zero();
        calcFireStartPos(&mFireStartPos[i], ::cLandingPosName[i]);
    }

    KoopaFunction::initKoopaAnimCamera(mKoopa, "DemoBattleStairsStart");
}

s32 KoopaBattleStairsVs1::registerStair(KoopaBattleMapStair* pBattleMapStair) {
    if (pBattleMapStair->isTypeDemoFar()) {
        mFarBattleMapStair = pBattleMapStair;
    }

    if (pBattleMapStair->isTypeDemoNear()) {
        mNearBattleMapStair = pBattleMapStair;
    }

    mStairsGroup->registerActor(pBattleMapStair);

    const s32 landing = ::getFireLanding(pBattleMapStair);
    if (pBattleMapStair->isTypeNormal()) {
        mStairNum[landing]++;
    }

    return calcFireAttackStep(pBattleMapStair, ::cFireSpeed, ::cStepToEmitFire, mFireStartPos[landing]);
}

void KoopaBattleStairsVs1::exeWaitDemo() {
    if (MR::tryStartDemo(mKoopa, "階段の戦い開始デモ")) {
        KoopaFunction::startKoopaAnimCamera(mKoopa, "DemoBattleStairsStart", ::cAnimCameraInterpoleFrame);
        KoopaFunction::endFaceCtrl(mKoopa, -1);

        MR::startAction(mKoopa, "DemoBattleStairsStart");
        MR::stopStageBGM(::cBgmStopFrame);

        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvDemo));
    }
}

void KoopaBattleStairsVs1::exeDemo() {
    if (MR::isStep(this, ::cDemoFarFireStep)) {
        ::emitDemoFireStairs(mKoopa, mFarBattleMapStair);
    }

    if (MR::isStep(this, ::cDemoNearFireStep)) {
        ::emitDemoFireStairs(mKoopa, mNearBattleMapStair);
    }

    if (MR::isBckStopped(mKoopa)) {
        KoopaFunction::endKoopaAnimCamera(mKoopa, "DemoBattleStairsStart", 0);
        MR::endDemo(mKoopa, "階段の戦い開始デモ");

        if (KoopaSwitchKeeper* pKeeper = KoopaFunction::getKoopaSwitchKeeper(mKoopa)) {
            MR::onSwitchB(pKeeper);
        }

        KoopaFunction::startFaceCtrl(mKoopa);
        MR::startStageBGM("MBGM_GALAXY_15_HURRY", false);

        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvWait));
    }
}

void KoopaBattleStairsVs1::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mKoopa, "Wait");
    }

    if (tryAttackRequest()) {
        MR::startAction(mKoopa, "AttackFire");
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvAttackFire));
    }
}

void KoopaBattleStairsVs1::exeAttackFire() {
    if (tryRequestedToMoveNextPos()) {
        mFireStair = nullptr;
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvJumpToNextPosStart));
        return;
    }

    if (MR::isStep(this, ::cStepToEmitFire)) {
        ::emitFireStairs(mKoopa, mFireStair);
        mFireStair = nullptr;
    }

    // A new request before the fire is out emits the pending stair's fire at once, then restarts the attack.
    KoopaBattleMapStair* pPendingStair = mFireStair;
    if (MR::isLessStep(this, ::cStepToEmitFire) && tryAttackRequest()) {
        ::emitFireStairs(mKoopa, pPendingStair);
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvAttackFire));
        return;
    }

    if (MR::isGreaterEqualStep(this, ::cStepToEmitFire) && tryAttackRequest()) {
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvAttackFire));
        return;
    }

    if (MR::isGreaterStep(this, ::cFireAttackStep)) {
        if (mIsJumpRequested || isAttackedAllStairs()) {
            mFireStair = nullptr;
            setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvJumpToNextPosStart));
            return;
        }

        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvWait));
    }
}

void KoopaBattleStairsVs1::exeJumpToNextPosStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mKoopa, "JumpStartFast");
        MR::startSound(mKoopa, "SE_BM_KOOPA_JUMP");
        mIsJumpRequested = false;
    }

    if (MR::isBckStopped(mKoopa)) {
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvJumpToNextPosLoop));
    }
}

void KoopaBattleStairsVs1::exeJumpToNextPosLoop() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mKoopa, "JumpFast");

        mOldPosition.set(mKoopa->mPosition);
        mLandingIndex++;

        Vec3f landRotate(0.0f, 0.0f, 0.0f);
        MR::findNamePos(::getJumpLandPosName(mLandingIndex), &mNewPosition, &landRotate);
        MR::getRotatedAxisZ(&mNewDirection, landRotate);
    }

    f32 rate = getNerveStep() / static_cast< f32 >(::cJumpTime);
    f32 ease = MR::getEaseInOutValue(rate, 0.0f, 1.0f, 1.0f);
    mKoopa->mPosition.set(mOldPosition * (1.0f - ease) + mNewPosition * ease);

    mKoopa->mPosition.y += ::cJumpHeight * MR::sinDegree(cNum_exeJumpToNextPosLoop_f1 * rate);
    MR::turnDirectionDegree(mKoopa, &mKoopa->mFront, mNewDirection, ::cTurnSpeed);

    if (MR::isStep(this, ::cJumpTime)) {
        MR::tryRumblePadStrong(mKoopa, PLAYER_1);
        MR::shakeCameraNormal();
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvJumpToNextPosEnd));
    }
}

void KoopaBattleStairsVs1::exeJumpToNextPosEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mKoopa, "LandFast");
        MR::startSound(mKoopa, "SE_BM_KOOPA_LAND");
    }

    // An attack cuts the landing short and snaps Koopa onto landing 1, or 2 for every later jump.
    if (tryAttackRequest()) {
        KoopaFunction::setKoopaPos(mKoopa, ::cLandingPosName[mLandingIndex == 1 ? 1 : 2]);
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvAttackFire));
        return;
    }

    if (MR::isBckStopped(mKoopa)) {
        KoopaFunction::setKoopaPos(mKoopa, ::getJumpLandPosName(mLandingIndex));
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvWait));
    }
}

// Every requesting stair is counted; the last one found is the one fired at.
bool KoopaBattleStairsVs1::tryAttackRequest() {
    bool isRequested = false;
    for (s32 i = 0; i < mStairsGroup->getObjNum(); i++) {
        KoopaBattleMapStair* pBattleMapStair = static_cast< KoopaBattleMapStair* >(mStairsGroup->getActor(i));

        if (pBattleMapStair->isRequestAttackVs1()) {
            isRequested = true;
            mFireStair = pBattleMapStair;
            mAttackCount++;
        }
    }

    return isRequested;
}

bool KoopaBattleStairsVs1::tryRequestedToMoveNextPos() {
    for (s32 i = 0; i < mStairsGroup->getObjNum(); i++) {
        const KoopaBattleMapStair* pBattleMapStair = static_cast< KoopaBattleMapStair* >(mStairsGroup->getActor(i));

        if (pBattleMapStair->isRequestAttackVs1()) {
            if (pBattleMapStair->mLevel > mLandingIndex) {
                return true;
            }

            if (pBattleMapStair->mIsJumpAfterAttack) {
                mIsJumpRequested = true;
            }
        }
    }

    return false;
}

// Koopa leaves a landing once he has answered as many attacks as there are normal stairs on it and below.
bool KoopaBattleStairsVs1::isAttackedAllStairs() const {
    if (mLandingIndex < 0 || mLandingIndex >= cLandingNum) {
        return false;
    }

    s32 stairNum = 0;
    for (s32 i = 0; i <= mLandingIndex; i++) {
        stairNum += mStairNum[i];
    }

    return mAttackCount >= stairNum;
}
