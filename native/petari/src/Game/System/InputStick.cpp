#include "Game/System/InputStick.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/LumaTime.hpp"
#include <nebula_geometry.hpp>

namespace {
    // A direction is on past this deflection.
    const f32 cIsOnValue = 0.2f;

    u32 calcDirections(const Vec2f& rStick) {
        u32 directions = 0;

        if (rStick.x > ::cIsOnValue) {
            directions |= InputStick::Direction_Right;
        }

        if (rStick.x < -::cIsOnValue) {
            directions |= InputStick::Direction_Left;
        }

        if (rStick.y > ::cIsOnValue) {
            directions |= InputStick::Direction_Up;
        }

        if (rStick.y < -::cIsOnValue) {
            directions |= InputStick::Direction_Down;
        }

        return directions;
    }
}  // namespace

InputStick::InputStick(const PlayerInput* pPad)
    : mPad(pPad), mStick(0.0f, 0.0f), mSpeed(), mHold(), mTrigger(), mRelease(), mCarriedTrigger(0), mCarriedRelease(0) {
}

void InputStick::update() {
    PlayerInputSample* pSample = mPad->getSample();

    if (!MR::hasMoveStick(pSample)) {
        return;
    }

    const Vec2f stickPrev(mStick);
    mStick.set(pSample->mStick.x, pSample->mStick.y);
    mSpeed = mStick.distance(stickPrev);

    const u32 directionsPrev = ::calcDirections(stickPrev);
    const u32 directions = ::calcDirections(mStick);
    mHold = directions;
    mTrigger = directions & ~directionsPrev;
    mRelease = directionsPrev & ~directions;
    if (luma::time::isStepWithheld()) {
        mCarriedTrigger |= mTrigger;
        mCarriedRelease |= mRelease;
    } else if ((mCarriedTrigger | mCarriedRelease) != 0) {
        mTrigger |= mCarriedTrigger;
        mRelease |= mCarriedRelease;
        mCarriedTrigger = mCarriedRelease = 0;
    }
}
