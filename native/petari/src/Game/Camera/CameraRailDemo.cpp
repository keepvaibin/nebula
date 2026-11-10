#include "Game/Camera/CameraRailDemo.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraRailHolder.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_placeOnRail_f1, "placeOnRail.f1", 1000.0f);

CameraRailDemo::CameraRailDemo(const char* pName)
    : Camera(pName), mRailRider(), mCalcType(), mDemoTime(), mDemoTimer(), mDampRatio(), mCoord() {
    lumacore::setClass(this, lumacore::kClassId< CameraRailDemo >);
}

void CameraRailDemo::reset() {
    mDemoTimer = 0;
    mCoord = 0.0f;
}

CameraTargetObj* CameraRailDemo::calc() {
    switch (mCalcType) {
    case 0:
        calcLinear();
        break;
    case 1:
        calcEaseInOut();
        break;
    case 2:
        calcDamp();
        break;
    }

    mDemoTimer++;
    return nullptr;
}

void CameraRailDemo::setParam(s32 zoneID, s32 railID, s32 calcType, s32 demoTime, f32 dampRatio) {
    mRailRider = MR::getCameraDirector()->mRailHolder->getRider(zoneID, railID);
    mCalcType = calcType;
    mDemoTime = demoTime;
    mDampRatio = dampRatio;
}

void CameraRailDemo::setParamFromChunk(const CameraParamChunk* pChunk) {
    setParam(pChunk->getZoneID(), pChunk->mGeneralParam->mNum1 >> 16, pChunk->mGeneralParam->mNum2, pChunk->mGeneralParam->mNum1 & 0xFFFF, pChunk->mGeneralParam->mDist);
}

void CameraRailDemo::calcLinear() {
    if (mRailRider == nullptr) {
        return;
    }

    placeOnRail(calcTimeRate() * mRailRider->getTotalLength());
}

void CameraRailDemo::calcEaseInOut() {
    if (mRailRider == nullptr) {
        return;
    }

    f32 t = calcTimeRate();
    t *= t;
    t *= t;

    mCoord += t * (mRailRider->getTotalLength() - mCoord);
    placeOnRail(mCoord);
}

void CameraRailDemo::calcDamp() {
    if (mRailRider == nullptr) {
        return;
    }

    mCoord = mCoord * mDampRatio + mRailRider->getTotalLength() * (1.0f - mDampRatio);
    placeOnRail(mCoord);
}

// 0 without a demo time, else the demo's elapsed part, at most 1.
f32 CameraRailDemo::calcTimeRate() const {
    if (mDemoTime <= 0) {
        return 0.0f;
    }

    if (mDemoTimer >= mDemoTime) {
        return 1.0f;
    }

    return static_cast< f32 >(mDemoTimer) / static_cast< f32 >(mDemoTime);
}

// The camera sits at coord and watches the rail point 1000 further on.
void CameraRailDemo::placeOnRail(f32 coord) {
    mRailRider->setCoord(coord);
    CameraLocalUtil::setPos(this, mRailRider->mCurPos);

    mRailRider->setCoord(coord + cNum_placeOnRail_f1);
    CameraLocalUtil::setWatchPos(this, mRailRider->mCurPos);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, Vec3f(0.0f, 1.0f, 0.0f));
}
