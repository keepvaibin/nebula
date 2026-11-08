#include "Game/System/InputRumble.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/InputRumbleData.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"

InputRumble** InputRumble::sInstanceForCallback;

void RumbleChannel::clear() {
    mPattern = nullptr;
    mSerial = 0;
    mStep = 0;
    mStrength = 0;
    mIsLoop = false;
    mOwner = nullptr;
}

void RumbleChannel::update() {
    if (mPattern == nullptr) {
        return;
    }

    if (mStep >= mPattern->mFrame) {
        if (!mIsLoop) {
            clear();
            return;
        }

        mStep = 0;
    }

    // How much of the frames around this one the pattern fills: its pulses are one soft buzz.
    const s32 cReach = 2;
    s32 on = 0;
    for (s32 frame = mStep - cReach; frame <= mStep + cReach; frame++) {
        s32 place = frame;
        if (place < 0 || place >= mPattern->mFrame) {
            if (!mIsLoop) {
                continue;
            }
            place = (place + mPattern->mFrame) % mPattern->mFrame;
        }

        on += mPattern->mPattern[place];
    }

    mStrength = static_cast< u8 >(mPattern->mPattern[mStep] != 0 ? 255 * (on + 1) / (2 * cReach + 2) : 255 * on / (2 * cReach + 2));
    mStep++;
}

void RumbleChannel::setPattern(const void* pParam1, const RumblePattern& rParam2, u32 param3, bool param4) {
    mSerial = param3;
    mStep = 0;
    mPattern = &rParam2;
    mOwner = pParam1;
    mIsLoop = param4;
}

InputRumble::InputRumble(PlayerInput* pPad) : mPad(pPad), mStrength(), mSerial(1), mStopTimer() {
    if (sInstanceForCallback == nullptr) {
        sInstanceForCallback = new InputRumble*[MR::getPlayerMaxCount()];

        for (u32 i = 0; i < MR::getPlayerMaxCount(); i++) {
            sInstanceForCallback[i] = nullptr;
        }

        RumbleData::initHashValue();
        RumbleData::checkHashCollision();
    }

    for (u8 i = 0; i < ARRAY_SIZE(mChannel); i++) {
        mChannel[i].clear();
    }
}

InputRumble::~InputRumble() {
    s32 chan = mPad->mChannel;

    if (mStrength != 0) {
        mStrength = 0;

        MR::setInputRumble(chan, 0);
    }

    sInstanceForCallback[chan] = nullptr;
}

void InputRumble::registInstance() {
    s32 chan = mPad->mChannel;

    sInstanceForCallback[chan] = this;

    if (mStrength != 0) {
        MR::setInputRumble(chan, mStrength);
    }
}

void InputRumble::pause() {
    MR::setInputRumble(mPad->mChannel, 0);
}

void InputRumble::stop() {
    mStrength = 0;

    pause();

    for (u8 i = 0; i < ARRAY_SIZE(mChannel); i++) {
        mChannel[i].clear();
    }
}

void InputRumble::update() {
    // A cutscene running unseen (NebulaCutsceneSkip) does not rumble.
    if (NebulaCutsceneSkip::isSkipping()) {
        bool isActive = mStrength != 0;
        for (u8 i = 0; i < ARRAY_SIZE(mChannel); i++) {
            isActive |= mChannel[i].mPattern != nullptr;
        }
        if (isActive) {
            stop();
        }
        return;
    }

    if (mStopTimer > 0) {
        mStopTimer--;

        stop();
    } else {
        InputRumble* pRumble = getRumbleInstance();

        if (pRumble != nullptr) {
            pRumble->updateRumble();
        }
    }
}

void InputRumble::updateRumble() {
    u8 strength = 0;

    for (u8 i = 0; i < ARRAY_SIZE(mChannel); i++) {
        mChannel[i].update();

        if (strength < mChannel[i].mStrength) {
            strength = mChannel[i].mStrength;
        }
    }

    if (strength != mStrength) {
        mStrength = strength;
        MR::setInputRumble(mPad->mChannel, strength);
    }
}

InputRumble* InputRumble::getRumbleInstance() const {
    return sInstanceForCallback[mPad->mChannel];
}

bool InputRumble::vibratePatternIfNotExist(const void* pParam1, const char* pPatternName) {
    return setRumblePatternIfNotExist(pParam1, *RumbleData::getPattern(pPatternName), false);
}

bool InputRumble::setRumblePatternIfNotExist(const void* pParam1, const RumblePattern& rParam2, bool param3) {
    s32 v1 = -1;
    s32 v2 = -1;
    s32 v3 = -1;

    findRubmlePattern(pParam1, &v1, &v2, &v3, rParam2);

    if (v1 != -1) {
        return false;
    }

    if (v2 != -1) {
        mSerial = (mSerial + 1) & 0x7FFFFFFF;

        mChannel[v2].setPattern(pParam1, rParam2, mSerial, param3);

        return true;
    }

    mSerial = (mSerial + 1) & 0x7FFFFFFF;

    mChannel[v3].setPattern(pParam1, rParam2, mSerial, param3);

    return true;
}

bool InputRumble::findRubmlePattern(const void* pOwner, s32* pExisting, s32* pEmpty, s32* pOldest, const RumblePattern& rPattern) {
    u32 oldest = -1;
    u8 oldestIndex = 0xFF;
    u8 emptyIndex = 0xFF;

    for (u8 i = 0; i < ARRAY_SIZE(mChannel); i++) {
        const RumblePattern* pPattern = mChannel[i].mPattern;

        if (pPattern == nullptr) {
            if (emptyIndex == 0xFF) {
                emptyIndex = i;
            }
        } else {
            if (pPattern->mHash == rPattern.mHash) {
                *pExisting = i;
                return true;
            }

            if (oldest > mChannel[i].mSerial) {
                oldest = mChannel[i].mSerial;
                oldestIndex = i;
            }
        }
    }

    if (emptyIndex != 0xFF) {
        *pEmpty = emptyIndex;
        return false;
    }

    *pOldest = oldestIndex;
    return false;
}
