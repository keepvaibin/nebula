#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"

void MarioActor::addRushSensor(HitSensor* pSensor, bool isNear) {
    if (!MR::isSensorAutoRush(pSensor) && !MR::isSensorRush(pSensor)) {
        return;
    }

    mRushSensors.at(mRushSensorCount) = pSensor;
    mRushSensorNear.at(mRushSensorCount) = isNear;
    mRushSensorCount++;
}

bool MarioActor::tryStandardRush() {
    bool automatic = true;
    if (isRequestRush()) {
        automatic = false;
    } else {
        HitSensor* target = getNearestRushTarget(false);
        if (target != nullptr) {
            if (mTakeCount != 0 && selectHandyRush(target)) {
                return false;
            }

            if (target->receiveMessage(ACTMES_IS_RUSH_REQUEST, getSensor("body"))) {
                automatic = false;
            }
        }
    }

    if (tryStartRush(automatic)) {
        resetSensorCount();
        mVelocity.zero();
        beginRush();
        return true;
    }

    return false;
}

void MarioActor::checkPriorRushTarget() {
    s32 maxVal = 0;
    s32 val = 0;
    for (s32 idx = 0; idx < mRushSensorCount; idx++) {
        if (isFixJumpRushSensor(mRushSensors[idx])) {
            continue;
        }

        u32 v = 0;
        switch (mRushSensors[idx]->mType) {
        case ATYPE_COCO_NUT_BIND:
            v = 10;
            break;
        }

        if (maxVal >= v) {
            continue;
        }

        maxVal = v;
        val = idx;
    }

    if (maxVal != 0) {
        mRushSensors[0] = mRushSensors[val];
        mRushSensorCount = 1;
    }
}

HitSensor* MarioActor::getNearestRushTarget(bool autoOnly) const {
    if (!mRushSensorCount) {
        return nullptr;
    }

    f32 maxRadius = 100000.0f;
    HitSensor* out = nullptr;

    if (!isEnableNerveChange()) {
        return nullptr;
    }

    if (!mMario->isEnableRush()) {
        HitSensor* target = getHighPriorityTarget();
        if (target == nullptr) {
            target = getRescueTarget();
        }

        if (target != nullptr) {
            Vec3f targetPos(target->mPosition);
            f32 size2;
            f32 size1 = (targetPos - mPosition).length();
            size2 = (targetPos - mCenterPos).length();
            f32 size3 = (targetPos - mHeadPos).length();

            if (size1 > size2) {
                size1 = size2;
            }

            if (size1 > size3) {
                size1 = size3;
            }

            if (autoOnly && target->mRadius < size1) {
                return nullptr;
            }

            return target;
        }

        if (!mMario->isHanging() && !mMario->isDamaging()) {
            for (s32 idx = 0; idx < mRushSensorCount; idx++) {
                if (selectHipDropRush(mRushSensors[idx])) {
                    return mRushSensors[idx];
                }
            }
        }

        return nullptr;
    }

    if (mHealth == 0) {
        return nullptr;
    }

    for (s32 idx = 0; idx < mRushSensorCount; idx++) {
        if (isFixJumpRushSensor(mRushSensors[idx])) {
            continue;
        }

        if (autoOnly && !MR::isSensorAutoRush(mRushSensors[idx])) {
            continue;
        }

        Vec3f pos(mRushSensors[idx]->mPosition);
        f32 size2;
        f32 size1 = (pos - mPosition).length();
        size2 = (pos - mCenterPos).length();
        f32 size3 = (pos - mHeadPos).length();

        if (size1 > size2) {
            size1 = size2;
        }

        if (size1 > size3) {
            size1 = size3;
        }

        if ((mRushSensorNear[idx] || !autoOnly || !(mRushSensors[idx]->mRadius + 20.0f < size1)) && size1 < maxRadius) {
            out = mRushSensors[idx];
            maxRadius = size1;
        }
    }

    return out;
}

HitSensor* MarioActor::getNearestJumpTarget() const {
    if (mRushSensorCount == 0) {
        return nullptr;
    }

    if (mHealth == 0) {
        return nullptr;
    }

    f32 nearestDistance = 100000.0f;
    HitSensor* nearest = nullptr;
    for (u32 i = 0; i < mRushSensorCount; i++) {
        if (!isFixJumpRushSensor(mRushSensors[i])) {
            continue;
        }

        if (mRushSensor == mRushSensors[i]) {
            continue;
        }

        Vec3f position(mRushSensors[i]->mPosition);
        f32 centerDistance;
        f32 distance = (position - mPosition).length();
        centerDistance = (position - mCenterPos).length();
        f32 headDistance = (position - mHeadPos).length();
        if (distance > centerDistance) {
            distance = centerDistance;
        }

        if (distance > headDistance) {
            distance = headDistance;
        }

        if ((mRushSensorNear[i] || !(mRushSensors[i]->mRadius < distance)) && distance < nearestDistance) {
            nearest = mRushSensors[i];
            nearestDistance = distance;
        }
    }

    return nearest;
}

HitSensor* MarioActor::getHighPriorityTarget() const {
    if (mRushSensorCount == 0) {
        return nullptr;
    }

    for (s32 idx = 0; idx < mRushSensorCount; idx++) {
        if (mRushSensors[idx]->isType(ATYPE_POWER_STAR_BIND)) {
            return mRushSensors[idx];
        }

        if (mHealth == 0) {
            continue;
        }

        if (mRushSensors[idx]->isType(ATYPE_QUESTION_COIN_BIND)) {
            return mRushSensors[idx];
        }

        if (mRushSensors[idx]->isType(ATYPE_PRIOR_BINDER)) {
            return mRushSensors[idx];
        }

        if (mRushSensors[idx]->isType(ATYPE_SCENARIO_STARTER_BIND)) {
            return mRushSensors[idx];
        }
    }

    return nullptr;
}

HitSensor* MarioActor::getRescueTarget() const {
    if (mRushSensorCount == 0) {
        return nullptr;
    }

    if (mHealth == 0) {
        return nullptr;
    }

    for (s32 idx = 0; idx < mRushSensorCount; idx++) {
        if (mRushSensors[idx]->isType(ATYPE_G_CAPTURE_BIND)) {
            return mRushSensors[idx];
        }

        if (selectOnDamageRush(mRushSensors[idx]->mHost->mName)) {
            return mRushSensors[idx];
        }
    }

    return nullptr;
}

bool MarioActor::tryStartRush(bool automatic) {
    if (mMario->isStatusActive(MarioStatus_Bury)) {
        return false;
    }

    if (mMario->isStatusActive(MarioStatus_Sukekiyo)) {
        return false;
    }

    HitSensor* target = getNearestRushTarget(automatic);
    if (getMovementStates().tornado && mMario->isRising() && target == mLastRushSensor) {
        return false;
    }

    if (target != nullptr) {
        for (;;) {
            u8 autoBind;
            if (automatic && selectAutoBind(target->mHost->mName, &autoBind)) {
                if (autoBind == 0) {
                    return false;
                }

                if (autoBind == 2) {
                    setPlayerMode(PlayerMode_Normal, true);
                }
            }

            if (target == mLastRushSensor && selectRebindTimer(target) && mFrameCount - mLastRushEndFrame < 120) {
                return false;
            }

            mRushSensors[0] = target;
            bool started;
            if (automatic) {
                started = target->receiveMessage(ACTMES_AUTORUSH_BEGIN, getSensor("body"));
            } else {
                started = target->receiveMessage(ACTMES_RUSH_BEGIN, getSensor("body"));
            }

            if (!started) {
                HitSensor* priority = getHighPriorityTarget();
                if (priority != nullptr && priority != target) {
                    target = priority;
                    continue;
                }
            }

            return started;
        }
    }

    return false;
}

bool MarioActor::tryJumpRush() {
    if (mHealth == 0) {
        return false;
    }

    if (mMarioAnim->isLandingAnimationRun()) {
        return false;
    }

    if (mMario->isStatusActive(MarioStatus_RecoveryOrWarp)) {
        return false;
    }

    if (mMario->isStatusActive(MarioStatus_Bury)) {
        return false;
    }

    HitSensor* jumpTarget = getNearestJumpTarget();
    if (jumpTarget != nullptr) {
        unsigned char autoBind;
        bool b = selectAutoBind(jumpTarget->mHost->mName, &autoBind);
        if (!b && !isActionOk("ジャンプ系バインド")) {
            return false;
        }

        if (mTakeCount != 0 && selectHandyRush(jumpTarget)) {
            return false;
        }

        if (b && autoBind == 0) {
            return false;
        }

        mRushSensors[0] = jumpTarget;

        bool out = jumpTarget->receiveMessage(ACTMES_AUTORUSH_BEGIN, getSensor("body"));

        if (out != false) {
            resetSensorCount();
            mVelocity.zero();
            beginRush();

            if (b && autoBind == 2) {
                setPlayerMode(0, true);
            }
        }

        return out;
    }

    return false;
}

void MarioActor::tryRushInRush() {
    HitSensor* target = getHighPriorityTarget();
    if (target == nullptr) {
        target = getNearestJumpTarget();
    }

    mRushSensorCount = 0;

    if (target == mRushSensor) {
        return;
    }

    if (target == nullptr) {
        return;
    }

    if (!target->receiveMessage(ACTMES_IS_RUSH_TAKEOVER, getSensor("body"))) {
        return;
    }

    HitSensor* rushSensor = mRushSensor;
    if (!mRushSensor->receiveMessage(ACTMES_RUSH_CANCEL, getSensor("body"))) {
        return;
    }

    if (!target->receiveMessage(ACTMES_AUTORUSH_BEGIN, getSensor("body"))) {
        if (mInRush) {
            const RushEndInfo rushEndInfo = RushEndInfo(nullptr, RushEndInfo::Mode_Release, Vec3f(0.0f, 0.0f, 0.0f), false, 0);
            endRush(&rushEndInfo);
        }

        return;
    }

    if (isFixJumpRushSensor(target) && isLandEffectRushSensor(rushSensor)) {
        mMario->mMovementStates.launchKind = false;

        playEffect("スーパースピンドライバ終了");
    }

    if (mInRush) {
        mRushSensor = target;

        if (mPlayerMode == 4 && selectHideFlyMeter(target)) {
            MR::getGameSceneLayoutHolder()->changeLifeMeterModeGround();
        }
    } else {
        mRushSensors[0] = target;

        beginRush();
    }
}
