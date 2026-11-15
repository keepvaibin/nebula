#include "Game/LiveActor/SpotMarkLight.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

SpotMarkLight::SpotMarkLight(LiveActor* pHost, f32 radius, f32 height, MtxPtr pMtx)
    : PartsModel(pHost, "マーク光柱", "SpotMarkLight", pMtx, -1, false), mIsDisappeared(false) {
    lumacore::setClass(this, lumacore::kClassId< SpotMarkLight >);
    if (!pMtx) {
        initFixedPosition(pHost->getBaseMtx(), Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    }

    mScale.z = radius / 50.0f;
    mScale.x = radius / 50.0f;
    mScale.y = height / 1000.0f;
    MR::startBrk(this, "Appear");
    MR::registerDemoSimpleCastAll(this);
}

void SpotMarkLight::control() {
    if (MR::isNearPlayerAnyTime(this, 1000.0f)) {
        if (!mIsDisappeared && MR::isBrkStopped(this)) {
            MR::startBrk(this, "Disappear");
            mIsDisappeared = true;
        }
    } else if (mIsDisappeared && MR::isBrkStopped(this)) {
        MR::startBrk(this, "Appear");
        mIsDisappeared = false;
    }
}

void SpotMarkLight::calcAndSetBaseMtx() {
    PartsModel::calcAndSetBaseMtx();
}
