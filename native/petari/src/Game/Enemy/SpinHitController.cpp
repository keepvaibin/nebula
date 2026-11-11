#include "Game/Enemy/SpinHitController.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_emitItem_f1, "emitItem.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_emitItem_f2, "emitItem.f2", 40.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_execute_1, "execute.1", 1);

namespace {
    NEBULA_TUNABLE(s32, cDefaultFlyStepMax, 15);
    NEBULA_TUNABLE(s32, cDefaultFlyStepMin, 10);
    NEBULA_TUNABLE(s32, cDefaultStopSceneFrames, 3);
    NEBULA_TUNABLE(f32, cDefaultGravityAccel, 0.0f);
    NEBULA_TUNABLE(f32, cDefaultHorizontalSpeed, 40.0f);
    NEBULA_TUNABLE(f32, cDefaultVerticalSpeed, 20.0f);
};  // namespace

SpinHitController::SpinHitController(const NameObj* pObj, s32 itemNum, bool isCoin) {
    setParameter(pObj, cDefaultFlyStepMax, cDefaultFlyStepMin, cDefaultStopSceneFrames, cDefaultGravityAccel, cDefaultHorizontalSpeed, cDefaultVerticalSpeed,
                 itemNum, isCoin);
}

SpinHitController::SpinHitController(const NameObj* pObj, s32 flyStepMax, s32 flyStepMin, s32 stopSceneFrames, f32 gravityAccel, f32 horizontalSpeed,
                                     f32 verticalSpeed, s32 itemNum, bool isCoin) {
    setParameter(pObj, flyStepMax, flyStepMin, stopSceneFrames, gravityAccel, horizontalSpeed, verticalSpeed, itemNum, isCoin);
}

void SpinHitController::setParameter(const NameObj*, s32 flyStepMax, s32 flyStepMin, s32 stopSceneFrames, f32 gravityAccel, f32 horizontalSpeed,
                                     f32 verticalSpeed, s32 itemNum, bool isCoin) {
    mFlyStepMax = flyStepMax;
    mFlyStepMin = flyStepMin;
    mStopSceneFrames = stopSceneFrames;
    mGravityAccel = gravityAccel;
    mHorizontalSpeed = horizontalSpeed;
    mVerticalSpeed = verticalSpeed;
    mIsCoin = isCoin;
    mItemNum = itemNum;
}

// Knocks the actor away from `rFrom` (toward `rTo`, across its gravity) and up, with the hit's scene stop.
void SpinHitController::start(LiveActor* pActor, const Vec3f& rFrom, const Vec3f& rTo) {
    Vec3f velocity(rTo);
    velocity -= rFrom;
    MR::vecKillElement(velocity, pActor->mGravity, &velocity);
    MR::normalizeOrZero(&velocity);
    velocity *= mHorizontalSpeed;

    Vec3f lift(pActor->mGravity);
    lift *= mVerticalSpeed;
    velocity -= lift;

    pActor->mVelocity.set(velocity);
    mIsStopScene = true;
}

void SpinHitController::startWithoutStopScene(LiveActor* pActor, const Vec3f& rFrom, const Vec3f& rTo) {
    start(pActor, rFrom, rTo);
    mIsStopScene = false;
}

// Flies until it lands (after the first step) or the max step, then dies; the items come out at the min step, or at
// death if that comes first. True once dead.
bool SpinHitController::execute(LiveActor* pActor) {
    if (MR::isFirstStep(pActor)) {
        MR::invalidateClipping(pActor);

        if (mIsStopScene) {
            MR::stopSceneForDefaultHit(mStopSceneFrames);
        }
    }

    Vec3f fall(pActor->mGravity);
    fall *= mGravityAccel;
    pActor->mVelocity.add(fall);

    if (MR::isStep(pActor, mFlyStepMin)) {
        emitItem(pActor);
    }

    if ((MR::isBinded(pActor) && MR::isGreaterStep(pActor, cStep_execute_1)) || MR::isGreaterStep(pActor, mFlyStepMax)) {
        MR::validateClipping(pActor);
        pActor->kill();

        if (MR::isLessStep(pActor, mFlyStepMin)) {
            emitItem(pActor);
        }

        return true;
    }

    return false;
}

// Once: the coins or star pieces.
void SpinHitController::emitItem(LiveActor* pActor) {
    if (mItemNum == 0) {
        return;
    }

    if (mIsCoin) {
        MR::appearCoinPop(pActor, pActor->mPosition, mItemNum);
    } else {
        MR::appearStarPiece(pActor, pActor->mPosition, mItemNum, cNum_emitItem_f1, cNum_emitItem_f2, false);
        if (pActor->mSoundObject != nullptr) {
            MR::startSound(pActor, "SE_OJ_STAR_PIECE_BURST");
        }
    }

    mItemNum = 0;
}
