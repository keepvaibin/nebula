#include "Game/MapObj/SpiderThreadWindCtrl.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    const f32 sWindAccelBattle = 0.5f;
    const f32 sWindAccelChance = 1.0f;
    const f32 sWindAccelBattleEndTarget = 2.0f;
    const f32 sWindAccelBattleEndCurrent = 100.0f;
    const f32 sWindAccelSpeedBattleEnd = 0.4f;
    static const s32 sTimeToStartWindMin = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 sTimeToStartWindMax = NebulaFrameClock::stepsOfSeconds(2);
    static const s32 sWindTimeMin = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 sWindTimeMax = NebulaFrameClock::stepsOfSeconds(2);
};  // namespace

SpiderThreadWindCtrl::SpiderThreadWindCtrl()
    : mWindTargetSpeed(::sWindAccelBattle), mWindSpeed(::sWindAccelBattle), mWindAccel(0.0f), mWindDirection(0.0f, 0.0f, -1.0f),
      mWind(mWindDirection) {
}

void SpiderThreadWindCtrl::update() {
    f32 target, speed, accel;
    speed = mWindSpeed;
    target = mWindTargetSpeed;

    // Toward the target by the accel a frame, never past it.
    if (speed != target) {
        accel = mWindAccel;
        if (speed < target) {
            speed += accel;
            if (speed > target) {
                speed = target;
            }
        } else {
            speed -= accel;
            if (speed < target) {
                speed = target;
            }
        }
        mWindSpeed = speed;
    }

    mWind.x = mWindDirection.x * mWindSpeed;
    mWind.y = mWindDirection.y * mWindSpeed;
    mWind.z = mWindDirection.z * mWindSpeed;
}

void SpiderThreadWindCtrl::startWindBattle() {
    mWindSpeed = ::sWindAccelBattle;
    mWindTargetSpeed = ::sWindAccelBattle;
    mWindAccel = 0.0f;
}

void SpiderThreadWindCtrl::startWindChance() {
    mWindSpeed = ::sWindAccelChance;
    mWindTargetSpeed = ::sWindAccelChance;
    mWindAccel = 0.0f;
}

void SpiderThreadWindCtrl::startWindBattleEnd() {
    mWindTargetSpeed = ::sWindAccelBattleEndTarget;
    mWindSpeed = ::sWindAccelBattleEndCurrent;
    mWindAccel = ::sWindAccelSpeedBattleEnd;
}

s32 SpiderThreadWindCtrl::getTimeToStartWind() const {
    if (mWindSpeed != mWindTargetSpeed) {
        return ::sTimeToStartWindMin;
    }

    return MR::getRandom(::sTimeToStartWindMin, ::sTimeToStartWindMax);
}

s32 SpiderThreadWindCtrl::getWindTime() const {
    return MR::getRandom(::sWindTimeMin, ::sWindTimeMax);
}
