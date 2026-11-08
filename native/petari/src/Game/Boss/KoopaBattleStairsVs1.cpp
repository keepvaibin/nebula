#include "Game/Boss/KoopaBattleStairsVs1.hpp"
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

namespace {
    inline KoopaFireStairs* emitFireStairs(Koopa* pKoopa, const KoopaBattleMapStair* pStair) {
        Vec3f jointPos(0.0f, 0.0f, 0.0f);
        MR::copyJointPos(pKoopa, "Tongue2", &jointPos);
        return KoopaFunction::emitFireStairsToTarget(pKoopa, pStair, jointPos, false);
    }

    static const s32 sAnimCameraInterpoleFrame = 60;
    static const s32 sStepToEmitFire = 15;
    static const f32 sFireSpeed = 20.0f;
    static const s32 sFireAttackStep = 33;
    static const s32 sStepJumpToNextPosLoop = 60;
    static const f32 sJumpHeight = 1000.0f;
    static const f32 sTurnSpeed = 2.0f;
    static const char* sKoopaPosName0 = "階段の戦い０（クッパ）";
    static const char* sKoopaPosName1 = "階段の戦い１（クッパ）";
    static const char* sKoopaPosName2 = "階段の戦い２（クッパ）";
    static const char* sKoopaPosNameEnd = "デモ中心";
};  // namespace

namespace NrvKoopaBattleStairsVs1 {
    NEW_NERVE(KoopaBattleStairsVs1NrvWaitDemo, KoopaBattleStairsVs1, WaitDemo);
    NEW_NERVE(KoopaBattleStairsVs1NrvDemo, KoopaBattleStairsVs1, Demo);
    NEW_NERVE(KoopaBattleStairsVs1NrvWait, KoopaBattleStairsVs1, Wait);
    NEW_NERVE(KoopaBattleStairsVs1NrvAttackFire, KoopaBattleStairsVs1, AttackFire);
    NEW_NERVE(KoopaBattleStairsVs1NrvJumpToNextPosStart, KoopaBattleStairsVs1, JumpToNextPosStart);
    NEW_NERVE(KoopaBattleStairsVs1NrvJumpToNextPosLoop, KoopaBattleStairsVs1, JumpToNextPosLoop);
    NEW_NERVE(KoopaBattleStairsVs1NrvJumpToNextPosEnd, KoopaBattleStairsVs1, JumpToNextPosEnd);
};  // namespace NrvKoopaBattleStairsVs1

KoopaBattleStairsVs1::KoopaBattleStairsVs1(Koopa* pKoopa)
    : KoopaBattleStairsBase(pKoopa), mFarBattleMapStair(), mNearBattleMapStair(), mFarFireStairs(), mNearFireStairs(), mFireStair(), mCanJump(),
      mJumpPos0(0.0f, 0.0f, 0.0f), mJumpPos1(0.0f, 0.0f, 0.0f), mJumpPos2(0.0f, 0.0f, 0.0f), mStairNum0(), mStairNum1(), mStairNum2(), mAvailableStairs(), mJumpIdx(),
      mOldPosition(0.0f, 0.0f, 0.0f), mNewPosition(0.0f, 0.0f, 0.0f), mNewDirection(0.0f, 0.0f, 1.0f) {
    initNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvWaitDemo));

    calcFireStartPos(&mJumpPos0, ::sKoopaPosName0);
    calcFireStartPos(&mJumpPos1, ::sKoopaPosName1);
    calcFireStartPos(&mJumpPos2, ::sKoopaPosName2);

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

    const Vec3f* vec = &mJumpPos0;

    if (pBattleMapStair->mArg1 == 1) {
        vec = &mJumpPos1;
        if (pBattleMapStair->isTypeNormal()) {
            mStairNum1++;
        }
    } else if (pBattleMapStair->mArg1 == 2) {
        vec = &mJumpPos2;
        if (pBattleMapStair->isTypeNormal()) {
            mStairNum2++;
        }
    } else if (pBattleMapStair->isTypeNormal()) {
        mStairNum0++;
    }

    return calcFireAttackStep(pBattleMapStair, ::sFireSpeed, ::sStepToEmitFire, *vec);
}

void KoopaBattleStairsVs1::exeWaitDemo() {
    if (MR::tryStartDemo(mKoopa, "階段の戦い開始デモ")) {
        KoopaFunction::startKoopaAnimCamera(mKoopa, "DemoBattleStairsStart", ::sAnimCameraInterpoleFrame);
        KoopaFunction::endFaceCtrl(mKoopa, -1);

        MR::startAction(mKoopa, "DemoBattleStairsStart");
        MR::stopStageBGM(60);

        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvDemo));
    }
}

void KoopaBattleStairsVs1::exeDemo() {
    if (MR::isStep(this, 264)) {
        mFarFireStairs = emitFireStairs(mKoopa, mFarBattleMapStair);

        MR::startSound(mKoopa, "SE_OJ_KOOPA_BULLET_SHOOT");

        MR::requestMovementOn(mFarFireStairs);
        MR::requestMovementOn(mFarBattleMapStair);
    }

    if (MR::isStep(this, 292)) {
        mNearFireStairs = emitFireStairs(mKoopa, mNearBattleMapStair);

        MR::startSound(mKoopa, "SE_OJ_KOOPA_BULLET_SHOOT");

        MR::requestMovementOn(mNearFireStairs);
        MR::requestMovementOn(mNearBattleMapStair);
    }

    if (MR::isBckStopped(mKoopa)) {
        KoopaFunction::endKoopaAnimCamera(mKoopa, "DemoBattleStairsStart", 0);
        MR::endDemo(mKoopa, "階段の戦い開始デモ");

        MR::onSwitchB(KoopaFunction::getKoopaSwitchKeeper(mKoopa));

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

    if (MR::isStep(this, ::sStepToEmitFire)) {
        emitFireStairs(mKoopa, mFireStair);
        mFireStair = nullptr;
    }

    KoopaBattleMapStair* pBattleMapStair = mFireStair;
    if (MR::isLessStep(this, ::sStepToEmitFire) && tryAttackRequest()) {
        emitFireStairs(mKoopa, pBattleMapStair);

        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvAttackFire));

        return;
    }

    if (MR::isGreaterEqualStep(this, ::sStepToEmitFire) && tryAttackRequest()) {
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvAttackFire));
        return;
    }

    if (MR::isGreaterStep(this, ::sFireAttackStep)) {
        if (mCanJump || (mJumpIdx == 0 && mAvailableStairs >= mStairNum0) || (mJumpIdx == 1 && mAvailableStairs >= mStairNum0 + mStairNum1) ||
            (mJumpIdx == 2 && mAvailableStairs >= mStairNum0 + mStairNum1 + mStairNum2)) {
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
        mCanJump = false;
    }

    if (MR::isBckStopped(mKoopa)) {
        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvJumpToNextPosLoop));
    }
}

// this is the only way I could match this
// TODO fix
void KoopaBattleStairsVs1::exeJumpToNextPosLoop() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mKoopa, "JumpFast");

        mOldPosition.set(mKoopa->mPosition);
        mJumpIdx++;
        Vec3f namePos = Vec3f(0.0f, 0.0f, 0.0f);

        if (mJumpIdx == 1) {
            MR::findNamePos(::sKoopaPosName1, &mNewPosition, &namePos);
        } else if (mJumpIdx == 2) {
            MR::findNamePos(::sKoopaPosName2, &mNewPosition, &namePos);
        } else {
            MR::findNamePos(::sKoopaPosNameEnd, &mNewPosition, &namePos);
        }

        MR::getRotatedAxisZ(&mNewDirection, namePos);
    }

    f32 step = getNerveStep() / 60.0f;
    f32 val = MR::getEaseInOutValue(step, 0.0f, 1.0f, 1.0f);
    mKoopa->mPosition.set(mOldPosition * (1.0f - val) + mNewPosition * val);

    mKoopa->mPosition.y += ::sJumpHeight * MR::sinDegree(180.0f * step);
    MR::turnDirectionDegree(mKoopa, &mKoopa->mFront, mNewDirection, ::sTurnSpeed);

    if (MR::isStep(this, ::sStepJumpToNextPosLoop)) {
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

    if (tryAttackRequest()) {
        if (mJumpIdx == 1) {
            KoopaFunction::setKoopaPos(mKoopa, ::sKoopaPosName1);
        } else {
            KoopaFunction::setKoopaPos(mKoopa, ::sKoopaPosName2);
        }

        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvAttackFire));
        return;
    }

    if (MR::isBckStopped(mKoopa)) {
        if (mJumpIdx == 1) {
            KoopaFunction::setKoopaPos(mKoopa, ::sKoopaPosName1);
        } else if (mJumpIdx == 2) {
            KoopaFunction::setKoopaPos(mKoopa, ::sKoopaPosName2);
        } else {
            KoopaFunction::setKoopaPos(mKoopa, ::sKoopaPosNameEnd);
        }

        setNerve(GET_NERVE(KoopaBattleStairsVs1, KoopaBattleStairsVs1NrvWait));
    }
}

bool KoopaBattleStairsVs1::tryAttackRequest() {
    KoopaBattleMapStair* pBattleMapStair;
    bool success = false;
    for (int idx = 0; idx < mStairsGroup->getObjNum(); idx++) {
        pBattleMapStair = static_cast< KoopaBattleMapStair* >(mStairsGroup->getActor(idx));

        if (pBattleMapStair->isRequestAttackVs1()) {
            success = true;
            mFireStair = pBattleMapStair;
            mAvailableStairs++;
        }
    }

    return success;
}

bool KoopaBattleStairsVs1::tryRequestedToMoveNextPos() {
    KoopaBattleMapStair* pBattleMapStair;
    for (int idx = 0; idx < mStairsGroup->getObjNum(); idx++) {
        pBattleMapStair = static_cast< KoopaBattleMapStair* >(mStairsGroup->getActor(idx));

        if (pBattleMapStair->isRequestAttackVs1()) {
            if (pBattleMapStair->mArg1 > mJumpIdx) {
                return true;
            }

            if (pBattleMapStair->mArg6) {
                mCanJump = true;
            }
        }
    }

    return false;
}

KoopaBattleStairsVs1::~KoopaBattleStairsVs1() {
}
