#include "Game/Map/LightPointCtrl.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_requestPointLight_f1, "requestPointLight.f1", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestPointLight_f2, "requestPointLight.f2", 0.999999f);
NEBULA_TUNABLE_NAMED(f32, cNum_clearPointLight_f1, "clearPointLight.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendPointLight_f1, "blendPointLight.f1", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_blendPointLight_f2, "blendPointLight.f2", 0.95f);

namespace {
    const s32 sDefaultBlendTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sDefaultDistRef, 15.0f);
};  // namespace

LightPointCtrl::LightPointCtrl()
    : mStep(-1), mBlendTime(::sDefaultBlendTime), mCurrentActor(), mPreviousActor(), mCandidateActor(), mCurrentInfo(), mTargetInfo(),
      mPreviousInfo() {
    mCurrentInfo = new PointLightInfo();
    mTargetInfo = new PointLightInfo();
    mPreviousInfo = new PointLightInfo();

    clearPointLight(mCurrentInfo);
    clearPointLight(mTargetInfo);
    clearPointLight(mPreviousInfo);
}

void LightPointCtrl::loadPointLight() {
    LightFunction::loadPointLightInfo(mCurrentInfo);
}

void LightPointCtrl::update() {
    if (mStep == -1) {
        mCurrentActor = mCandidateActor;
        tryBlendStart();
    }

    updatePointLight();

    if (mStep == -1) {
        mPreviousActor = mCurrentActor;
        *mPreviousInfo = *mTargetInfo;
        mCandidateActor = nullptr;
    }
}

void LightPointCtrl::requestPointLight(const LiveActor* pActor, Vec3f pos, Color8 color, f32 intensity, s32 duration) {
    {
        // Every request, for a renderer that lights from each source (the game keeps one, below). Its reach as the
        // game's own falloff gives it: half the light at the reference distance times sqrt(b / (1 - b)).
        const f32 brightness = MR::clamp(intensity, 0.95f, 0.999999f);
        NebulaFramePointLight light{};
        light.position[0] = pos.x;
        light.position[1] = pos.y;
        light.position[2] = pos.z;
        light.half_distance = ::sDefaultDistRef * MR::sqrt(brightness / (1.0f - brightness));
        light.color[0] = static_cast< f32 >(color.r) / 255.0f;
        light.color[1] = static_cast< f32 >(color.g) / 255.0f;
        light.color[2] = static_cast< f32 >(color.b) / 255.0f;
        // The glowing body's size: the actor's own (its binder's sphere) where it has one.
        light.radius = pActor != nullptr && pActor->getBinder() != nullptr ? MR::getBinderRadius(pActor) : 0.0f;
        Frame::pointLight(light);
    }
    if (mStep == -1 && isUpdateCandidateActor(pActor)) {
        mCandidateActor = pActor;
        mTargetInfo->mPos = pos;
        mTargetInfo->mColor = color.mRGBA;
        mTargetInfo->mRefBrightness = MR::clamp(intensity, cNum_requestPointLight_f1, cNum_requestPointLight_f2);
        mTargetInfo->mRefDistance = ::sDefaultDistRef;
        mTargetInfo->mDistAttnFn = LIGHT_FALLOFF_STEEP;
        mBlendTime = duration >= 0 ? duration : ::sDefaultBlendTime;
    }
}

void LightPointCtrl::updatePointLight() {
    if (mCurrentActor == nullptr && mPreviousActor != nullptr) {
        clearPointLight(mTargetInfo);
    }

    if (mCurrentActor == nullptr && mStep == -1) {
        clearPointLight(mCurrentInfo);
        return;
    }

    if (getStep() == -1) {
        *mCurrentInfo = *mTargetInfo;
        return;
    }

    // A blend of no time is at its target at once (0 / 0 would load a NaN light for a frame).
    f32 t = mBlendTime > 0 ? MR::getEaseInOutValue(static_cast< f32 >(mStep) / mBlendTime, 0.0f, 1.0f, 1.0f) : 1.0f;
    blendPointLight(mCurrentInfo, *mPreviousInfo, *mTargetInfo, t);

    if (mStep < mBlendTime) {
        mStep++;
    } else {
        mStep = -1;
    }
}

void LightPointCtrl::clearPointLight(PointLightInfo* pInfo) {
    Vec pos;
    pos.x = 0.0f;
    pos.y = 0.0f;
    pos.z = 0.0f;
    pInfo->mPos = pos;

    NebulaColor8 color;
    color.r = 0;
    color.g = 0;
    color.b = 0;
    color.a = 255;
    pInfo->mColor = color;

    pInfo->mRefBrightness = cNum_clearPointLight_f1;
    pInfo->mRefDistance = ::sDefaultDistRef;
    pInfo->mDistAttnFn = LIGHT_FALLOFF_STEEP;
    mBlendTime = ::sDefaultBlendTime;
}

void LightPointCtrl::blendPointLight(PointLightInfo* pDst, const PointLightInfo& rStart, const PointLightInfo& rEnd, f32 t) {
    if (mCurrentActor == nullptr) {
        pDst->mPos = rStart.mPos;
    } else if (mPreviousActor == nullptr) {
        pDst->mPos = rEnd.mPos;
    } else {
        MR::blendVec(&pDst->mPos, rStart.mPos, rEnd.mPos, t);
    }

    f32 start = mPreviousActor == nullptr ? cNum_blendPointLight_f1 : rStart.mRefBrightness;
    f32 end = mCurrentActor == nullptr ? cNum_blendPointLight_f2 : rEnd.mRefBrightness;
    pDst->mRefBrightness = MR::getLinerValue(t, start, end, 1.0f);
    MR::blendColor(&pDst->mColor, rStart.mColor, rEnd.mColor, t);
    pDst->mRefDistance = MR::getLinerValue(t, rStart.mRefDistance, rEnd.mRefDistance, 1.0f);
}

bool LightPointCtrl::tryBlendStart() {
    if (mPreviousActor == mCurrentActor) {
        return false;
    }

    mStep = 0;
    return true;
}

bool LightPointCtrl::isUpdateCandidateActor(const LiveActor* pActor) const {
    if (mCandidateActor == nullptr) {
        return true;
    }

    f32 dist = MR::calcDistanceToPlayer(mCandidateActor);
    return MR::calcDistanceToPlayer(pActor) < dist;
}
