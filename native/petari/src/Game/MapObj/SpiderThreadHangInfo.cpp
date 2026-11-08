#include "Game/MapObj/SpiderThreadHangInfo.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpiderThreadPart.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"

SpiderThreadHangInfo::SpiderThreadHangInfo()
    : mBindedActor(nullptr), mPosition(nullptr), mThreadPart(nullptr), mHangIndex(-1), mIsHanging(false), mPriority(0) {
}

void SpiderThreadHangInfo::startBind(LiveActor* pActor, const Vec3f* pPos, SpiderThreadPart* pThreadPart, s32 hangIndex, const Vec3f& rVel,
                                     s32 priority) {
    mBindedActor = pActor;
    mPosition = pPos;
    mThreadPart = pThreadPart;
    mHangIndex = hangIndex;
    mIsHanging = false;
    mPriority = priority;

    if (!MR::isNearZero(rVel)) {
        mThreadPart->touchActor(mHangIndex, rVel);
    }
}

void SpiderThreadHangInfo::endBind() {
    if (mIsHanging) {
        endHang(false);
    }

    mBindedActor = nullptr;
    mThreadPart = nullptr;
    mHangIndex = -1;
    mIsHanging = false;
}

void SpiderThreadHangInfo::startHang() {
    mIsHanging = true;
    mThreadPart->startHang(mHangIndex);
}

void SpiderThreadHangInfo::endHang(bool isCancel) {
    if (isCancel) {
        MR::sendSimpleMsgToActor(ACTMES_HANG_CANCEL, mBindedActor);
    }

    mThreadPart->endHang(mHangIndex);
    mIsHanging = false;
}

void SpiderThreadHangInfo::updateHang() {
    mThreadPart->updateHang(*mPosition, mHangIndex);
}

void SpiderThreadHangInfo::copyInfo(const SpiderThreadHangInfo* pOther) {
    *this = *pOther;
}
