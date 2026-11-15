#include "Game/MapObj/CrystalCage.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/DummyDisplayModel.hpp"
#include "Game/Util.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cSensorRadius = 130.0f;
    const f32 cSmallClippingRadius = 300.0f;
    const f32 cClippingRadius = 600.0f;
    // The ground is looked for this far above and below.
    const f32 cGroundSearchHeight = 300.0f;
    const f32 cLargeGroundSearchHeight = 1000.0f;
    const f32 cBinderRadius = 50.0f;
    const f32 cBinderLift = 50.0f;
    const f32 cBinderFallSpeed = 2.0f;
    const f32 cLargeStarHeight = 250.0f;
    const s32 cLargeHitPoint = 2;
    // Hits after one that does not break it are ignored this long.
    const s32 cHitInvalidSteps = 20;
    // A small cage's sensor slides along its axis to the point nearest the player.
    const f32 cSmallSensorBottom = -40.0f;
    const f32 cSmallSensorTop = 140.0f;
    // A super spin driver comes out a little after the cage breaks.
    const s32 cSuperSpinDriverDeadStep = 10;
    const s32 cBreakHitStopStep = 2;
    const s32 cBreakHitStopFrames = 3;
};  // namespace

namespace NrvCrystalCage {
    NEW_NERVE(CrystalCageNrvWait, CrystalCage, Wait);
    NEW_NERVE(CrystalCageNrvBreak, CrystalCage, Break);
    NEW_NERVE(CrystalCageNrvBreakAfter, CrystalCage, BreakAfter);
};  // namespace NrvCrystalCage

CrystalCage::CrystalCage(const char* pName)
    : LiveActor(pName), mCrystalCageType(CrystalCageType_Small), mBreakObj(nullptr), mHitPoint(1), mHitInvalidTime(0), mRumbleCalc(nullptr),
      mRumbleDir(0.0f, 0.0f, 1.0f), mBasePos(gZeroVec), mSensorPos(gZeroVec), mDisplayModel(nullptr), mGroundPos(gZeroVec), mBreakOnlyIfShown(false),
      mPowerStarId(-1), mVanishOnBreak(false), mPlayRiddleSFX(false), mHasBinding(false), mBinderOffset(gZeroVec) {
    lumacore::setClass(this, lumacore::kClassId< CrystalCage >);
    mBreakMtx.identity();
}

// A crystal holding an item (DummyDisplayModel arg 7) that a spin breaks. The large cage holds a power star, takes two
// hits and cannot be pushed through; small and medium ones break on any enemy attack too, and leave a broken base
// unless arg 0 makes them vanish.
void CrystalCage::init(const TableIter& rIter) {
    const char* pObjName;
    MR::getObjectName(&pObjName, rIter);
    initMapToolInfo(rIter);
    MR::calcGravity(this);
    initModel(pObjName);
    MR::connectToSceneCrystal(this);
    mSensorPos.set(mPosition);
    initHitSensor(1);
    MR::addHitSensorPosMapObj(this, "body", 8, (cSensorRadius * mScale.x), &mSensorPos, Vec3f(0.0f, 0.0f, 0.0f));

    if (mCrystalCageType != CrystalCageType_Small) {
        MR::invalidateHitSensors(this);
    }

    if (mHasBinding) {
        initBinder(cBinderRadius, 0.0f, 0);
        MR::setBinderOffsetVec(this, &mBinderOffset, false);
        MR::setBinderExceptActor(this, this);
    }

    if (mCrystalCageType != CrystalCageType_Large) {
        initEffectKeeper(0, nullptr);
    }

    MR::initCollisionParts(this, pObjName, getSensor("body"), nullptr);

    f32 clippingRadius = mCrystalCageType == CrystalCageType_Small ? cSmallClippingRadius : cClippingRadius;
    MR::setClippingTypeSphere(this, clippingRadius * mScale.x);
    initSound(4, false);

    if (mCrystalCageType == CrystalCageType_Large) {
        f32 starHeight = mScale.x * cLargeStarHeight;
        mHitPoint = cLargeHitPoint;

        mDisplayModel =
            MR::createDummyDisplayModelCrystalItem(this, DummyDisplayModelId_PowerStar, Vec3f(0.0f, starHeight, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));

        if (mPowerStarId == -1) {
            MR::declarePowerStar(this);
        } else {
            MR::declarePowerStar(this, mPowerStarId);
        }
    } else {
        Vec3f itemOffset;

        if (mCrystalCageType == CrystalCageType_Small) {
            itemOffset.set< f32 >(-30.0f, 100.0f, -30.0f);
        } else {
            itemOffset.set< f32 >(0.0f, 200.0f, 0.0f);
        }

        mDisplayModel = MR::createDummyDisplayModelCrystalItem(this, rIter, itemOffset, Vec3f(0.0f, 0.0f, 0.0f));

        if (mDisplayModel != nullptr) {
            switch (MR::getDummyDisplayModelId(rIter, -1)) {
            case DummyDisplayModelId_Coin:
                MR::declareCoin(this, 1);
                break;
            case DummyDisplayModelId_StarPiece:
                MR::startBva(mDisplayModel, "Freeze");
                break;
            }
        }
    }

    MR::startBva(this, pObjName);
    MR::setBvaFrameAndStop(this, 0.0f);

    if (mHitPoint > 1) {
        mRumbleCalc = new RumbleCalculatorCosMultLinear(4.0f, HALF_PI, 50.0f, 20);
    }

    MR::joinToGroupArray(this, rIter, nullptr, 32);
    initNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait));

    if (MR::tryRegisterDemoCast(this, rIter) && mDisplayModel != nullptr) {
        MR::tryRegisterDemoCast(mDisplayModel, rIter);
    }

    if (mCrystalCageType == CrystalCageType_Large) {
        makeActorAppeared();
        return;
    }

    if (MR::isExistStageSwitchSleep(rIter)) {
        MR::useStageSwitchSleep(this, rIter);
        makeActorDead();
        return;
    }

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

// Finds the ground under the cage for the broken base (and the binder), searching along its up both ways.
void CrystalCage::initAfterPlacement() {
    if (mVanishOnBreak && !mHasBinding) {
        return;
    }

    f32 searchHeight = mCrystalCageType == CrystalCageType_Large ? cLargeGroundSearchHeight : cGroundSearchHeight;
    Vec3f up;
    Vec3f lineDir;
    Vec3f lineStart;
    MR::calcUpVec(&up, this);
    lineStart.add(mPosition, up * (searchHeight * mScale.x));
    lineDir.scale((-(2.0f * searchHeight) * mScale.x), up);

    if (!MR::getFirstPolyOnLineToMapExceptActor(&mGroundPos, 0, lineStart, lineDir, this)) {
        mGroundPos.set(mPosition);
    }

    if (mHasBinding) {
        mBinderOffset.sub(mGroundPos, mPosition);
        mBinderOffset.add(up * cBinderLift);
        mVelocity.scale(-cBinderFallSpeed, up);
    }
}

void CrystalCage::kill() {
    LiveActor::kill();
    mBreakObj->kill();

    if (mDisplayModel != nullptr) {
        mDisplayModel->kill();
    }
}

// Switch A breaks it, or (arg 3) removes it unseen when it is clipped.
void CrystalCage::forceBreak() {
    if (!isNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait))) {
        return;
    }

    if (!mBreakOnlyIfShown || !MR::isClipped(this)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        startBreak();
    } else {
        kill();
    }
}

void CrystalCage::startBreak() {
    MR::invalidateClipping(this);
    MR::invalidateCollisionParts(this);
    MR::invalidateHitSensors(this);
    setNerve(GET_NERVE(CrystalCage, CrystalCageNrvBreak));
}

void CrystalCage::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mCrystalCageType == CrystalCageType_Small) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

// The last hit point breaks it; an earlier hit bounces the attacker, cracks the cage and shakes it.
bool CrystalCage::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait)) || !MR::isMsgPlayerHitAll(msg)) {
        return false;
    }

    if (mHitPoint <= 0 || mHitInvalidTime != 0) {
        return false;
    }

    if (mHitPoint == 1) {
        startBreak();
        MR::startCSSound("CS_SPIN_HIT", nullptr, 0);
        return true;
    }

    if (!MR::sendArbitraryMsg(ACTMES_REFLECT, pSender, pReceiver)) {
        return false;
    }

    mHitInvalidTime = cHitInvalidSteps;
    mHitPoint = mHitPoint - 1;

    MR::setBvaFrameAndStop(this, 1.0f);
    MR::startSound(this, mCrystalCageType == CrystalCageType_Large ? "SE_OJ_CRYSTAL_CAGE_L_DAMAGE" : "SE_OJ_CRYSTAL_CAGE_S_DAMAGE");

    mRumbleCalc->start(0);
    mRumbleDir.sub(pSender->mPosition, pReceiver->mPosition);
    MR::normalize(&mRumbleDir);
    return true;
}

bool CrystalCage::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (mCrystalCageType != CrystalCageType_Large && isNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait))) {
        startBreak();
        return true;
    }

    return false;
}

// The large cage's arg 0 is its power star; the others' args 0-3 are vanish-on-break, the riddle sound, binding to the
// ground and break-only-if-shown.
void CrystalCage::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mBasePos.set(mPosition);

    if (MR::isEqualObjectName(rIter, "CrystalCageS")) {
        mCrystalCageType = CrystalCageType_Small;
    } else if (MR::isEqualObjectName(rIter, "CrystalCageM")) {
        mCrystalCageType = CrystalCageType_Medium;
    } else {
        mCrystalCageType = CrystalCageType_Large;
    }

    if (mCrystalCageType == CrystalCageType_Large) {
        MR::getPlacementArg0NoInit(rIter, &mPowerStarId);
        return;
    }

    MR::useStageSwitchWriteDead(this, rIter);

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &CrystalCage::forceBreak));
    }

    MR::getPlacementArg0NoInit(rIter, &mVanishOnBreak);
    MR::getPlacementArg1NoInit(rIter, &mPlayRiddleSFX);
    MR::getPlacementArg2NoInit(rIter, &mHasBinding);
    MR::getPlacementArg3NoInit(rIter, &mBreakOnlyIfShown);
}

void CrystalCage::initModel(const char* pName) {
    initModelManagerWithAnm(pName, nullptr, false);
    MR::makeMtxUpNoSupportPos(&mBreakMtx, -mGravity, mBasePos);

    if (mCrystalCageType == CrystalCageType_Large) {
        mBreakObj = MR::createModelObjMapObjStrongLight("クリスタルケージ[大]壊れモデル", "CrystalCageLBreak", mBreakMtx.toMtxPtr());
    } else {
        mBreakObj = MR::createModelObjMapObjStrongLight("クリスタルケージ[小]壊れモデル", "CrystalCageSBreak", mBreakMtx.toMtxPtr());
    }

    MR::invalidateClipping(mBreakObj);
    mBreakObj->mScale.set(mScale);
    MR::registerDemoSimpleCastAll(mBreakObj);
    mBreakObj->makeActorDead();
}

void CrystalCage::tryOnSwitchDead() {
    if (mCrystalCageType == CrystalCageType_Large || !MR::isValidSwitchDead(this)) {
        return;
    }

    s32 deadStep = 0;

    if (mDisplayModel != nullptr && MR::getDummyDisplayModelId(mDisplayModel) == DummyDisplayModelId_SuperSpinDriver) {
        deadStep = cSuperSpinDriverDeadStep;
    }

    if (MR::isStep(this, deadStep)) {
        MR::onSwitchDead(this);
    }
}

void CrystalCage::exeWait() {
    if (mHitInvalidTime > 0) {
        mHitInvalidTime--;
    }

    if (mRumbleCalc != nullptr) {
        if (mRumbleCalc->mFrame < mRumbleCalc->mDuration) {
            mRumbleCalc->calc();
            Vec3f shake;
            shake.scale(mRumbleCalc->mValue.y, mRumbleDir);
            mPosition.add(mBasePos, shake);
        } else {
            mPosition.set(mBasePos);
        }
    }

    if (mCrystalCageType == CrystalCageType_Small) {
        Vec3f up;
        MR::calcUpVec(&up, this);
        Vec3f bottom;
        bottom.scale(cSmallSensorBottom, up);
        bottom.add(mPosition);
        Vec3f top;
        top.scale(cSmallSensorTop, up);
        top.add(mPosition);
        MR::calcPerpendicFootToLineInside(&mSensorPos, *MR::getPlayerPos(), bottom, top);
    }
}

void CrystalCage::exeBreak() {
    if (MR::isFirstStep(this)) {
        if (mVanishOnBreak) {
            MR::hideModel(this);
        } else {
            MR::setBvaFrameAndStop(this, mCrystalCageType == CrystalCageType_Large ? 2.0f : 1.0f);
            mPosition.set(mGroundPos);
        }

        if (mHasBinding) {
            MR::offBind(this);
            mVelocity.zero();
        }

        mBreakObj->appear();
        MR::startBck(mBreakObj, "Break");

        if (mCrystalCageType == CrystalCageType_Large) {
            MR::startSound(this, "SE_OJ_CRYSTAL_CAGE_L_BREAK");

            // The star appears where its crystal model hung; a mod may have removed that model.
            const Vec3f& rStarPos = mDisplayModel != nullptr ? mDisplayModel->mPosition : mPosition;

            if (mPowerStarId == -1) {
                MR::requestAppearPowerStar(this, rStarPos);
            } else {
                MR::requestAppearPowerStar(this, mPowerStarId, rStarPos);
            }
        } else {
            MR::emitEffect(this, "Break");
            MR::startSound(this, "SE_OJ_CRYSTAL_CAGE_S_BREAK");

            if (mPlayRiddleSFX) {
                MR::startSystemSE("SE_SY_READ_RIDDLE_S");
            }
        }

        if (mDisplayModel != nullptr) {
            switch (MR::getDummyDisplayModelId(mDisplayModel)) {
            case DummyDisplayModelId_Coin:
                MR::appearCoinPop(this, mDisplayModel->mPosition, 1);
                break;
            case DummyDisplayModelId_Tico:
                MR::startSound(this, "SE_OJ_CRY_CAGE_MV_TICO_APR");
                break;
            case DummyDisplayModelId_Kinopio:
                MR::startSound(this, "SE_SV_KINOPIO_PUHA");
                break;
            }

            mDisplayModel->kill();
        }
    }

    if (mCrystalCageType != CrystalCageType_Large && MR::isStep(this, cBreakHitStopStep)) {
        MR::stopScene(cBreakHitStopFrames);
    }

    tryOnSwitchDead();

    if (MR::isBckStopped(mBreakObj)) {
        if (mVanishOnBreak) {
            kill();
        } else {
            setNerve(GET_NERVE(CrystalCage, CrystalCageNrvBreakAfter));
        }
    }
}

void CrystalCage::exeBreakAfter() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        mBreakObj->kill();
    }
}
