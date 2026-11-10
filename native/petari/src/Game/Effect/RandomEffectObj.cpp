#include "Game/Effect/RandomEffectObj.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getFarClipDistance_f1, "getFarClipDistance.f1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getClippingRadius_f1, "getClippingRadius.f1", 1500.0f);

namespace {
    static const s32 sLifeTime = NebulaFrameClock::stepsOfSeconds(10);
    NEBULA_TUNABLE(s32, sRandWidth, 180);
};  // namespace

RandomEffectObj::RandomEffectObj(const char* pName) : SimpleEffectObj(pName), mStep(), mSeStep(-1), mLifeTime(::sLifeTime), mRandWidth(::sRandWidth), mIsFireworks(false) {
    lumacore::setClass(this, lumacore::kClassId< RandomEffectObj >);
}

void RandomEffectObj::init(const TableIter& rIter) {
    SimpleEffectObj::init(rIter);
    mIsFireworks = MR::isEqualString(mObjName, "FireworksA");
    MR::getPlacementArg0NoInit(rIter, &mLifeTime);
    MR::getPlacementArg1NoInit(rIter, &mRandWidth);

    mStep = getRandomTime();
}

void RandomEffectObj::appear() {
    LiveActor::appear();

    mStep = getRandomTime();
}

s32 RandomEffectObj::getRandomTime() const {
    s32 time = MR::repeat(mPosition.z + (mPosition.x + mPosition.y), 0.0f, mRandWidth);
    return time < 0 ? -time : time;
}

void RandomEffectObj::control() {
    if (!isStateMove()) {
        return;
    }

    if (mStep < 0) {
        return;
    }

    if (mSeStep >= 0) {
        mSeStep++;

        if (mIsFireworks) {
            if (mSeStep == 40) {
                MR::startSound(this, "SE_OJ_FIREWORK_SEA_EXPLODE");
            }
        }
    }

    mStep--;

    if (mStep != 0) {
        return;
    }

    MR::emitEffect(this, mObjName);

    if (mIsFireworks) {
        MR::startSound(this, "SE_OJ_FIREWORK_SEA_SHOOT");
    }

    mStep = MR::getRandom(mLifeTime - mRandWidth, mLifeTime + mRandWidth);
    mSeStep = 0;
}

f32 RandomEffectObj::getFarClipDistance() const {
    return cNum_getFarClipDistance_f1;
}

f32 RandomEffectObj::getClippingRadius() const {
    return cNum_getClippingRadius_f1;
}
