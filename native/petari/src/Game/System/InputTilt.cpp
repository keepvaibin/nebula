#include "Game/System/InputTilt.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/MathUtil.hpp"

InputTilt::InputTilt(const PlayerInput* pPad, u32 device) : mPad(pPad), _4(device), _624(-1), _628(0) {
}

bool InputTilt::getAcceleration(TVec3f* pOut) const {
    if (_628 <= 0) {
        pOut->zero();
        return false;
    }

    pOut->set(mHistory[_624]);
    return true;
}

bool InputTilt::getPastAcceleration(TVec3f* pOut, s32 index) const {
    if (index >= _628) {
        pOut->zero();
        return false;
    }

    s32 slot = _624 - index;

    if (slot < 0) {
        slot += 128;
    }

    pOut->set(mHistory[slot]);
    return true;
}

void InputTilt::update() {
    s32 statusCount = mPad->getValidStatusCount();
    if (statusCount <= 0) {
        TVec3f previous;

        if (_624 == -1) {
            previous.set< f32 >(0.0f, 0.0f, 0.0f);
        } else {
            previous = mHistory[_624];
        }

        _624++;

        if (_624 >= 128U) {
            _624 = 0;
        }

        mHistory[_624] = previous;

        if (_628 < 128U) {
            _628++;
        }
    }

    for (s32 i = statusCount - 1; i >= 0; i--) {
        PlayerInputSample* pSample = mPad->getSample(i);

        if (pSample == nullptr) {
            continue;
        }

        TVec3f acceleration;

        if (_4 == 0) {
            acceleration = pSample->mAccel;
        } else {
            if (!MR::hasMoveStick(pSample)) {
                continue;
            }

            acceleration = pSample->mSubTilt;
        }

        _624++;

        if (_624 >= 128U) {
            _624 = 0;
        }

        mHistory[_624] = acceleration;

        if (_628 < 128U) {
            _628++;
        }
    }

}
