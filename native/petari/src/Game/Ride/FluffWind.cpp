#include "Game/Ride/FluffWind.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcWindInfo_f1, "calcWindInfo.f1", 1000000.0f);

namespace {
    NEBULA_TUNABLE(f32, sEffectEmitterInterval, 600.0f);
    static const s32 sStepBlowWindMin = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sStepBlowWindMax = NebulaFrameClock::stepsOfSeconds(4);
    static const s32 sStepToValidateClipping = NebulaFrameClock::stepsOfSeconds(3);

    FluffWindHolder* getFluffWindHolder() {
        return MR::getSceneObj< FluffWindHolder >(SceneObj_FluffWindHolder);
    }
};  // namespace

namespace NrvFluffWindEffect {
    NEW_NERVE(FluffWindEffectNrvBlowWind, FluffWindEffect, BlowWind);
};  // namespace NrvFluffWindEffect

FluffWindEffect::FluffWindEffect(const char* pName) : LiveActor(pName), mEffectName(), mTimer(-1) {
    lumacore::setClass(this, lumacore::kClassId< FluffWindEffect >);
    mMtx.identity();
}

void FluffWindEffect::initEffectInfo(const Vec3f& rPos, const Vec3f& rFront, const Vec3f& rUp, const char* pEffectName, f32 radius) {
    mPosition.set(rPos);
    MR::connectToSceneMapObjMovement(this);
    MR::makeMtxFrontUpPos(&mMtx, rFront, rUp, rPos);
    mEffectName = pEffectName;
    initEffectKeeper(0, pEffectName);
    MR::setEffectHostMtx(this, mEffectName, mMtx);
    initNerve(GET_NERVE(FluffWindEffect, FluffWindEffectNrvBlowWind));
    if (radius > 0.0f) {
        MR::setClippingTypeSphere(this, radius);
    }
}

void FluffWindEffect::init(const TableIter&) {
    makeActorAppeared();
}

void FluffWindEffect::makeActorDead() {
    LiveActor::makeActorDead();
    MR::deleteEffect(this, mEffectName);
}

void FluffWindEffect::exeBlowWind() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, mEffectName);
        mTimer = MR::getRandom(::sStepBlowWindMin, ::sStepBlowWindMax);
    }

    if (MR::isStep(this, ::sStepToValidateClipping)) {
        MR::validateClipping(this);
    }

    if (MR::isStep(this, mTimer)) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(FluffWindEffect, FluffWindEffectNrvBlowWind));
    }
}

FluffWind::FluffWind(const char* pName) : LiveActor(pName), mNumEffects(), mEffects() {
    lumacore::setClass(this, lumacore::kClassId< FluffWind >);
}

void FluffWind::init(const TableIter& rIter) {
    initRailRider(rIter);
    initEffectKeeper(0, "FluffWind");
    MR::invalidateClipping(this);
    MR::createSceneObj(SceneObj_FluffWindHolder);
    ::getFluffWindHolder()->registerActor(this);

    s32 count = MR::getRailTotalLength(this) / ::sEffectEmitterInterval;
    mNumEffects = count + 1;
    // A rail shorter than one interval holds a single effect at its start (the count was 0).
    f32 stepDistance = MR::getRailTotalLength(this) / (mNumEffects > 1 ? mNumEffects - 1 : 1);
    mEffects = new FluffWindEffect*[mNumEffects];
    for (s32 i = 0; i < mNumEffects; i++) {
        f32 currentRailPos = i * stepDistance;
        Vec3f pos;
        Vec3f dir;
        MR::calcRailPosAndDirectionAtCoord(&pos, &dir, this, currentRailPos);

        FluffWindEffect* effect = new FluffWindEffect("わたげエフェクト");
        effect->initEffectInfo(pos, dir, Vec3f(0.0f, 1.0f, 0.0f), "FluffWind", -1.0f);
        effect->initWithoutIter();

        mEffects[i] = effect;
    }

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor(this, &FluffWind::makeActorDead));
    }

    makeActorAppeared();
}

void FluffWind::makeActorDead() {
    for (s32 i = 0; i < mNumEffects; i++) {
        mEffects[i]->makeActorDead();
    }
    LiveActor::makeActorDead();
}

FluffWindHolder::FluffWindHolder() : LiveActorGroup("わたげ風", 8) {
    lumacore::setClass(this, lumacore::kClassId< FluffWindHolder >);
}

void FluffWindHolder::calcWindInfo(const Vec3f& rPosition, Vec3f* pWindDirection, f32* pWindDistance) const {
    f32 bestDistance = cNum_calcWindInfo_f1;
    Vec3f bestDir(0.0f, 0.0f, 0.0f);

    for (s32 i = 0; i < getObjNum(); i++) {
        LiveActor* pActor = getActor(i);

        Vec3f railDir(0.0f, 0.0f, 0.0f);
        Vec3f nearestPos;
        MR::calcNearestRailPosAndDirection(&nearestPos, &railDir, pActor, rPosition);

        f32 distance = nearestPos.distance(rPosition);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestDir = railDir;
        }
    }

    pWindDirection->set(bestDir);
    *pWindDistance = bestDistance;
}

bool FluffFunction::calcFluffWindInfo(const Vec3f& rPosition, Vec3f* pWindDirection, f32* pWindDistance) {
    if (!MR::isExistSceneObj(SceneObj_FluffWindHolder)) {
        pWindDirection->zero();
        *pWindDistance = -1.0f;

        return false;
    }

    ::getFluffWindHolder()->calcWindInfo(rPosition, pWindDirection, pWindDistance);

    return true;
}
