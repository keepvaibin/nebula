#include "Game/MapObj/CrystalCage.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/DummyDisplayModel.hpp"
#include "Game/Util.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>


namespace NrvCrystalCage {
    NEW_NERVE(CrystalCageNrvWait, CrystalCage, Wait);
    NEW_NERVE(CrystalCageNrvBreak, CrystalCage, Break);
    NEW_NERVE(CrystalCageNrvBreakAfter, CrystalCage, BreakAfter);
};  // namespace NrvCrystalCage

CrystalCage::CrystalCage(const char* pName)
    : LiveActor(pName), mCrystalCageType(0), mBreakObj(nullptr), mHitPoint(1), mHitInvalidTime(0), mRumbleCalc(nullptr), mRumbleDir(0.0f, 0.0f, 1.0f), mBasePos(gZeroVec),
      mSensorPos(gZeroVec), mDisplayModel(nullptr), mGroundPos(gZeroVec), mBreakOnlyIfShown(0), mPowerStarId(-1), mIsBreakObjVisible(false), mPlayRiddleSFX(false), mHasBinding(false),
      mBinderOffset(gZeroVec) {
    mBreakMtx.identity();
}

void CrystalCage::init(const TableIter& rIter) {
    const char* obj_name;
    MR::getObjectName(&obj_name, rIter);
    initMapToolInfo(rIter);
    MR::calcGravity(this);
    initModel(obj_name);
    MR::connectToSceneCrystal(this);
    mSensorPos.set(mPosition);
    initHitSensor(1);
    MR::addHitSensorPosMapObj(this, "body", 8, (130.0f * mScale.x), &mSensorPos, Vec3f(0.0f, 0.0f, 0.0f));

    if (mCrystalCageType) {
        MR::invalidateHitSensors(this);
    }

    if (mHasBinding) {
        initBinder(50.0f, 0.0f, 0);
        MR::setBinderOffsetVec(this, &mBinderOffset, false);
        MR::setBinderExceptActor(this, this);
    }

    if (mCrystalCageType != 2) {
        initEffectKeeper(0, nullptr);
    }

    MR::initCollisionParts(this, obj_name, getSensor("body"), nullptr);

    f32 range = !mCrystalCageType ? 300.0f : 600.0f;
    MR::setClippingTypeSphere(this, range * mScale.x);
    initSound(4, false);

    Vec3f v23;
    if (mCrystalCageType == 2) {
        f32 mult = mScale.x * 250.0f;
        mHitPoint = 2;

        mDisplayModel = MR::createDummyDisplayModelCrystalItem(this, 7, Vec3f(0.0f, mult, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));

        if (mPowerStarId == -1) {
            MR::declarePowerStar(this);
        } else {
            MR::declarePowerStar(this, mPowerStarId);
        }
    } else {
        if (!mCrystalCageType) {
            v23.set< f32 >(-30.0f, 100.0f, -30.0f);
        } else {
            v23.set< f32 >(0.0f, 200.0f, 0.0f);
        }

        mDisplayModel = MR::createDummyDisplayModelCrystalItem(this, rIter, v23, Vec3f(0.0f, 0.0f, 0.0f));

        if (mDisplayModel) {
            switch (MR::getDummyDisplayModelId(rIter, -1)) {
            case 0:
                MR::declareCoin(this, 1);
                break;
            case 4:
                MR::startBva(mDisplayModel, "Freeze");
            }
        }
    }

    MR::startBva(this, obj_name);
    MR::setBvaFrameAndStop(this, 0.0f);

    if (mHitPoint > 1) {
        mRumbleCalc = new RumbleCalculatorCosMultLinear(4.0f, HALF_PI, 50.0f, 0x14);
    }

    MR::joinToGroupArray(this, rIter, nullptr, 0x20);
    initNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait));

    if (MR::tryRegisterDemoCast(this, rIter)) {
        if (mDisplayModel) {
            MR::tryRegisterDemoCast(mDisplayModel, rIter);
        }
    }

    if (mCrystalCageType == 2) {
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

void CrystalCage::initAfterPlacement() {
    if (!mIsBreakObjVisible || mHasBinding) {
        f32 val = mCrystalCageType == 2 ? 1000.0f : 300.0f;
        Vec3f up_vec;
        Vec3f stack_2C;
        Vec3f stack_20;
        MR::calcUpVec(&up_vec, this);
        // TODO: fix this during .scale pass
        stack_20.add(mPosition, up_vec * (val * mScale.x));
        stack_2C.scale((-(2.0f * val) * mScale.x), up_vec);

        if (!MR::getFirstPolyOnLineToMapExceptActor(&mGroundPos, 0, stack_20, stack_2C, this)) {
            mGroundPos.set(mPosition);
        }

        if (mHasBinding) {
            mBinderOffset.sub(mGroundPos, mPosition);
            mBinderOffset.add(up_vec * 50.0f);
            mVelocity.scale(-2.0f, up_vec);
        }
    }
}

void CrystalCage::kill() {
    LiveActor::kill();
    mBreakObj->kill();

    if (mDisplayModel) {
        mDisplayModel->kill();
    }
}

void CrystalCage::forceBreak() {
    if (isNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait))) {
        if (!mBreakOnlyIfShown || !MR::isClipped(this)) {
            MR::tryRumblePadStrong(this, PLAYER_1);
            MR::invalidateClipping(this);
            MR::invalidateCollisionParts(this);
            MR::invalidateHitSensors(this);
            setNerve(GET_NERVE(CrystalCage, CrystalCageNrvBreak));
        } else {
            kill();
        }
    }
}

void CrystalCage::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!mCrystalCageType) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool CrystalCage::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait))) {
        return false;
    }

    if (!MR::isMsgPlayerHitAll(msg)) {
        return false;
    }

    if (mHitPoint > 0 && !mHitInvalidTime) {
        if (mHitPoint == 1) {
            MR::invalidateClipping(this);
            MR::invalidateCollisionParts(this);
            MR::invalidateHitSensors(this);
            setNerve(GET_NERVE(CrystalCage, CrystalCageNrvBreak));
            MR::startCSSound("CS_SPIN_HIT", nullptr, 0);
            return true;
        }

        if (MR::sendArbitraryMsg(ACTMES_REFLECT, pSender, pReceiver)) {
            mHitInvalidTime = 20;
            mHitPoint = mHitPoint - 1;

            MR::setBvaFrameAndStop(this, 1.0f);
            if (mCrystalCageType == 2) {
                MR::startSound(this, "SE_OJ_CRYSTAL_CAGE_L_DAMAGE");
            } else {
                MR::startSound(this, "SE_OJ_CRYSTAL_CAGE_S_DAMAGE");
            }

            mRumbleCalc->start(0);
            mRumbleDir.sub(pSender->mPosition, pReceiver->mPosition);
            MR::normalize(&mRumbleDir);

            return true;
        }
    }

    return false;
}

bool CrystalCage::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mCrystalCageType != 2 && isNerve(GET_NERVE(CrystalCage, CrystalCageNrvWait))) {
        MR::invalidateClipping(this);
        MR::invalidateCollisionParts(this);
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(CrystalCage, CrystalCageNrvBreak));
        return true;
    }

    return false;
}

void CrystalCage::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mBasePos.set(mPosition);

    if (MR::isEqualObjectName(rIter, "CrystalCageS")) {
        mCrystalCageType = 0;
    } else if (MR::isEqualObjectName(rIter, "CrystalCageM")) {
        mCrystalCageType = 1;
    } else {
        mCrystalCageType = 2;
    }

    if (mCrystalCageType == 2) {
        MR::getPlacementArg0NoInit(rIter, &mPowerStarId);
    } else {
        MR::useStageSwitchWriteDead(this, rIter);

        if (MR::useStageSwitchReadA(this, rIter)) {
            MR::listenStageSwitchOnA(this, MR::Functor(this, &CrystalCage::forceBreak));
        }

        MR::getPlacementArg0NoInit(rIter, &mIsBreakObjVisible);
        MR::getPlacementArg1NoInit(rIter, &mPlayRiddleSFX);
        MR::getPlacementArg2NoInit(rIter, &mHasBinding);
        MR::getPlacementArg3NoInit(rIter, &mBreakOnlyIfShown);
    }
}

void CrystalCage::initModel(const char* pName) {
    initModelManagerWithAnm(pName, nullptr, false);
    MR::makeMtxUpNoSupportPos(&mBreakMtx, -mGravity, mBasePos);

    if (mCrystalCageType == 2) {
        mBreakObj = MR::createModelObjMapObjStrongLight("クリスタルケージ[大]壊れモデル", "CrystalCageLBreak", (MtxPtr)&mBreakMtx);
    } else {
        mBreakObj = MR::createModelObjMapObjStrongLight("クリスタルケージ[小]壊れモデル", "CrystalCageSBreak", (MtxPtr)&mBreakMtx);
    }

    MR::invalidateClipping(mBreakObj);
    mBreakObj->mScale.set(mScale);
    MR::registerDemoSimpleCastAll(mBreakObj);
    mBreakObj->makeActorDead();
}

void CrystalCage::tryOnSwitchDead() {
    if (mCrystalCageType == 2 || !MR::isValidSwitchDead(this)) {
        return;
    }

    s32 step = 0;

    if (mDisplayModel && MR::getDummyDisplayModelId(mDisplayModel) == 3) {
        step = 10;
    }

    if (MR::isStep(this, step)) {
        MR::onSwitchDead(this);
    }
}

void CrystalCage::exeWait() {
    if (mHitInvalidTime > 0) {
        mHitInvalidTime--;
    }

    if (mRumbleCalc) {
        if (mRumbleCalc->mFrame < mRumbleCalc->mDuration) {
            mRumbleCalc->calc();
            Vec3f v9;
            v9.scale(mRumbleCalc->mValue.y, mRumbleDir);
            mPosition.add(mBasePos, v9);
        } else {
            mPosition.set(mBasePos);
        }
    }

    if (mCrystalCageType == 0) {
        Vec3f up_vec;
        MR::calcUpVec(&up_vec, this);
        Vec3f v6;
        v6.scale(-40.0f, up_vec);
        v6.add(mPosition);
        Vec3f v5;
        v5.scale(140.0f, up_vec);
        v5.add(mPosition);
        MR::calcPerpendicFootToLineInside(&mSensorPos, *MR::getPlayerPos(), v6, v5);
    }
}

void CrystalCage::exeBreak() {
    if (MR::isFirstStep(this)) {
        if (mIsBreakObjVisible) {
            MR::hideModel(this);
        } else {
            MR::setBvaFrameAndStop(this, mCrystalCageType == 2 ? 2.0f : 1.0f);
            mPosition.set(mGroundPos);
        }

        if (mHasBinding) {
            MR::offBind(this);
            mVelocity.zero();
        }

        mBreakObj->appear();
        MR::startBck(mBreakObj, "Break");

        if (mCrystalCageType == 2) {
            MR::startSound(this, "SE_OJ_CRYSTAL_CAGE_L_BREAK");
            if (mPowerStarId == -1) {
                MR::requestAppearPowerStar(this, mDisplayModel->mPosition);
            } else {
                MR::requestAppearPowerStar(this, mPowerStarId, mDisplayModel->mPosition);
            }
        } else {
            MR::emitEffect(this, "Break");
            MR::startSound(this, "SE_OJ_CRYSTAL_CAGE_S_BREAK");

            if (mPlayRiddleSFX) {
                MR::startSystemSE("SE_SY_READ_RIDDLE_S");
            }
        }

        if (mDisplayModel && !MR::getDummyDisplayModelId(mDisplayModel)) {
            MR::appearCoinPop(this, mDisplayModel->mPosition, 1);
        }

        if (mDisplayModel && MR::getDummyDisplayModelId(mDisplayModel) == 5) {
            MR::startSound(this, "SE_OJ_CRY_CAGE_MV_TICO_APR");
        }

        if (mDisplayModel && MR::getDummyDisplayModelId(mDisplayModel) == 1) {
            MR::startSound(this, "SE_SV_KINOPIO_PUHA");
        }

        if (mDisplayModel) {
            mDisplayModel->kill();
        }
    }

    if (mCrystalCageType != 2 && MR::isStep(this, 2)) {
        MR::stopScene(3);
    }

    tryOnSwitchDead();

    if (MR::isBckStopped(mBreakObj)) {
        if (mIsBreakObjVisible) {
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
