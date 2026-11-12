#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_math.h>

const f32 sOne = 1.0f;
const f32 sZero = 0.0f;
const f32 sSameDirEpsilon = 0.01f;
const f32 sNearZeroEpsilon = 0.001f;
const f32 sHipDropSlideAngle = 0.7853982f;
const f32 sHipDropSlideRot = 0.10471976f;
const f32 sHipDropSlideLen = 10.0f;
const f32 sJumpDropSlideDotMin = 0.1f;
const f32 sHopperJumpSlideDotMin = 0.707f;

void Mario::delTask(MarioModuleTask* pTask) {
    MarioModuleTask* next = pTask->mNext;
    if (next == nullptr) {
        return;
    }

    MarioModuleTask* head = mTaskHead;
    if (head == pTask) {
        if (next == pTask) {
            mTaskHead = nullptr;
        } else {
            mTaskHead = next;
        }

        pTask->mNext = nullptr;
        pTask->end();
        return;
    }

    while (true) {
        if (head == nullptr) {
            return;
        }

        MarioModuleTask* headNext = head->mNext;
        if (headNext == pTask) {
            if (next == pTask) {
                head->mNext = head;
            } else {
                head->mNext = next;
            }

            pTask->mNext = nullptr;
            pTask->end();
            return;
        }

        if (headNext == head) {
            return;
        }
    }
}

void Mario::execTask() {
    MarioModuleTask* task = mTaskHead;

    while (true) {
        if (task == nullptr) {
            return;
        }

        MarioModuleTask* next = task->mNext;
        if (!task->exec()) {
            delTask(task);
        }

        if (next == task) {
            return;
        }

        task = next;
    }
}

void Mario::drawTask() const {
    MarioModuleTask* task = mTaskHead;

    while (true) {
        if (task == nullptr) {
            return;
        }

        MarioModuleTask* next = task->mNext;
        task->draw();
        if (next == task) {
            return;
        }

        task = next;
    }
}

void Mario::initTask() {
    for (u32 i = 0; i < ARRAY_SIZE(mExtraTasks); i++) {
        mExtraTasks[i] = nullptr;
    }

    mTaskHead = nullptr;
}

bool Mario::isActiveTask(Task task) {
    for (u32 i = 0; i < ARRAY_SIZE(mExtraTasks); i++) {
        if (mExtraTasks[i] == task) {
            return true;
        }
    }

    return false;
}

bool Mario::isActiveTaskID(u32 id) {
    for (u32 i = 0; i < ARRAY_SIZE(mExtraTasks); i++) {
        if (mExtraTasks[i] != nullptr) {
            if (mExtraTaskFlags[i] & id) {
                return true;
            }
        }
    }

    return false;
}

bool Mario::pushTask(Task task, u32 flags) {
    if (isActiveTask(task)) {
        return false;
    }

    u32 index = 0;
    for (; index < ARRAY_SIZE(mExtraTasks); index++) {
        if (mExtraTasks[index] == nullptr) {
            break;
        }
    }

    mExtraTasks[index] = task;
    mExtraTaskFlags[index] = flags;
    return true;
}

void Mario::popTask(Task task) {
    for (u32 i = 0; i < ARRAY_SIZE(mExtraTasks); i++) {
        if (mExtraTasks[i] == task) {
            mExtraTasks[i] = nullptr;
            return;
        }
    }
}

void Mario::callExtraTasks(u32 flags) {
    execTask();

    for (u32 i = 0; i < ARRAY_SIZE(mExtraTasks); i++) {
        if (mExtraTasks[i] == nullptr) {
            continue;
        }

        if (flags != 0 && !(flags & mExtraTaskFlags[i])) {
            continue;
        }

        if (!(this->*mExtraTasks[i])(mExtraTaskFlags[i])) {
            Task task = mExtraTasks[i];
            popTask(task);
        }
    }
}

void Mario::startHandy() {
    pushTask(&Mario::taskOnHandy, 0x40);
}

bool Mario::taskOnHandy(u32) {
    if (!mActor->mTakeCount) {
        stopEffect("いい汗");
        return false;
    }

    if (mTargetWalkSpeedIndex > 2) {
        playEffect("いい汗");
    } else {
        stopEffect("いい汗");
    }

    return true;
}

void Mario::startHipDropBlur() {
    if (isPlayerModeHopper()) {
        if (gIsLuigi) {
            playEffect("ホッパー尻落ルイージ");
        } else {
            playEffect("ホッパー尻落");
        }

        pushTask(&Mario::taskOnHipDropBlurHopper, 0x80);
        return;
    }

    if (gIsLuigi) {
        playEffect("尻落ルイージ");
    } else {
        playEffect("尻落");
    }

    pushTask(&Mario::taskOnHipDropBlur, 0x80);
}

bool Mario::taskOnHipDropBlurHopper(u32) {
    if (!getPlayer()->mMovementStates.hipDrop || mMovementStates.onGround || isStatusActive(MarioStatus_Swim)) {
        if (gIsLuigi) {
            stopEffect("ホッパー尻落ルイージ");
        } else {
            stopEffect("ホッパー尻落");
        }

        return false;
    }

    return true;
}

bool Mario::taskOnHipDropBlur(u32) {
    if (!getPlayer()->mMovementStates.hipDrop || mMovementStates.onGround || isStatusActive(MarioStatus_Swim)) {
        if (gIsLuigi) {
            stopEffect("尻落ルイージ");
        } else {
            stopEffect("尻落");
        }

        return false;
    }

    return true;
}

bool Mario::taskOnHipDropSlide(u32 flags) {
    if (flags == 0x100) {
        if (!getPlayer()->mMovementStates.hipDrop) {
            return false;
        }
    }

    if (MR::diffAngleAbs(mHipDropSlideGravity, getAirGravityVec()) > sHipDropSlideAngle) {
        return false;
    }

    if (getPlayer()->mMovementStates.onGround || !getPlayer()->mMovementStates.jumping || isStatusActive(MarioStatus_Swim)) {
        return false;
    }

    HitSensor* sensor = mHipDropSlideSensor;
    bool enabled = sensor->mValidByHost != 0 && sensor->mValidBySystem != 0;

    if (!enabled) {
        return false;
    }

    Vec3f dir(mPosition);
    dir -= mHipDropSlideSensorStart;
    dir.setLength(mHipDropSlideSpeed);

    Vec3f dirCopy(dir);
    MR::normalizeOrZero(&dir);

    Vec3f reverseGravity = -mHipDropSlideGravity;
    if (dir.dot(reverseGravity) <= sZero) {
        return false;
    }

    Mtx rot;
    NebulaMtxRotAxisRad(rot, &mHipDropSlideSide, sHipDropSlideRot);
    NebulaMtxMultVecSR(rot, &dir, &dir);
    dir.setLength(mHipDropSlideSpeed);
    dir -= dirCopy;
    dir.setLength(sHipDropSlideLen);

    cutGravityElementFromJumpVec(true);
    cutVecElementFromJumpVec(mHipDropSlideGravity);
    addVelocity(dir);

    dirCopy.setLength(sOne);
    addVelocity(dirCopy);
    return true;
}

bool Mario::taskOnFreezeEnd(u32) {
    return mActor->finalizeFreezeModel();
}

void Mario::startHipDropSlide(const HitSensor* pSensor) {
    if (isActiveTaskID(0x100)) {
        return;
    }

    if (isActiveTaskID(0x200)) {
        return;
    }

    pushTask(&Mario::taskOnHipDropSlide, 0x100);

    Vec3f dir(mPosition - pSensor->mPosition);

    if (MR::isSameDirection(dir, getAirGravityVec(), sSameDirEpsilon) || MR::isNearZero(dir, sNearZeroEpsilon)) {
        dir = -mFrontVec;
    }

    MR::normalizeOrZero(&dir);

    mHipDropSlideSide.cross(-getAirGravityVec(), dir);
    MR::normalizeOrZero(&mHipDropSlideSide);

    mHipDropSlideSensor = const_cast< HitSensor* >(pSensor);
    mHipDropSlideSensorStart = pSensor->mPosition;
    mHipDropSlideSpeed = pSensor->mRadius;
    mHipDropSlideGravity = getAirGravityVec();

    changeAnimation("ヒップドロップ滑り");
}

void Mario::startJumpDropSlide(const HitSensor* pSensor) {
    if (isActiveTaskID(0x100)) {
        return;
    }

    if (isActiveTaskID(0x200)) {
        return;
    }

    Vec3f dir(mPosition - pSensor->mPosition);
    MR::normalizeOrZero(&dir);

    f32 dot = dir.dot(-getAirGravityVec());
    if (dot <= sJumpDropSlideDotMin) {
        return;
    }

    pushTask(&Mario::taskOnHipDropSlide, 0x200);

    Vec3f slideDir(mPosition - pSensor->mPosition);

    if (MR::isSameDirection(slideDir, getAirGravityVec(), sSameDirEpsilon) || MR::isNearZero(slideDir, sNearZeroEpsilon)) {
        slideDir = -mFrontVec;
    }

    MR::normalizeOrZero(&slideDir);

    mHipDropSlideSide.cross(-getAirGravityVec(), slideDir);
    MR::normalizeOrZero(&mHipDropSlideSide);

    mHipDropSlideSensor = const_cast< HitSensor* >(pSensor);
    mHipDropSlideSensorStart = pSensor->mPosition;
    mHipDropSlideSpeed = pSensor->mRadius;
    mHipDropSlideGravity = getAirGravityVec();

    bool changeSlideAnimation = !isPlayerModeHopper();
    if (changeSlideAnimation) {
        if (dot > sHopperJumpSlideDotMin) {
            f32 frontDot = mFrontVec.dot(slideDir);
            if (frontDot > sZero) {
                changeAnimation("ジャンプ順滑り");
            } else {
                changeAnimation("ジャンプ逆滑り");
            }
        }
    }

    mJumpVec.zero();
}

void Mario::startFreezeEnd() {
    pushTask(&Mario::taskOnFreezeEnd, 0x800);
}