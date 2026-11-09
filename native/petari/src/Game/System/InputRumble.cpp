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

void RumbleChannel::setPattern(const void* pOwner, const RumblePattern& rPattern, u32 serial, bool isLoop) {
    mSerial = serial;
    mStep = 0;
    mPattern = &rPattern;
    mOwner = pOwner;
    mIsLoop = isLoop;
}

InputRumble::InputRumble(PlayerInput* pPad) : mPad(pPad), mStrength(), mSerial(1), mStopTimer() {
    if (sInstanceForCallback == nullptr) {
        sInstanceForCallback = new InputRumble*[MR::getPlayerMaxCount()]();
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

    mChannels.clear();
}

void InputRumble::update() {
    // A cutscene running unseen (NebulaCutsceneSkip) does not rumble.
    if (NebulaCutsceneSkip::isSkipping()) {
        bool isActive = mStrength != 0;
        for (const RumbleChannel* pChannel = mChannels.begin(); pChannel != mChannels.end(); pChannel++) {
            isActive |= pChannel->mPattern != nullptr;
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

    for (RumbleChannel* pChannel = mChannels.begin(); pChannel != mChannels.end(); pChannel++) {
        pChannel->update();

        if (strength < pChannel->mStrength) {
            strength = pChannel->mStrength;
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

bool InputRumble::vibratePatternIfNotExist(const void* pOwner, const char* pPatternName) {
    return setRumblePatternIfNotExist(pOwner, *RumbleData::getPattern(pPatternName), false);
}

// A pattern already playing is left alone; a new one takes a free channel, else a new one.
bool InputRumble::setRumblePatternIfNotExist(const void* pOwner, const RumblePattern& rPattern, bool isLoop) {
    s32 existing = -1;
    s32 empty = -1;

    findRumblePattern(pOwner, &existing, &empty, rPattern);

    if (existing != -1) {
        return false;
    }

    if (empty == -1) {
        RumbleChannel channel;
        channel.clear();
        empty = mChannels.size();
        mChannels.push_back(channel);
    }

    mSerial = (mSerial + 1) & 0x7FFFFFFF;
    mChannels[empty].setPattern(pOwner, rPattern, mSerial, isLoop);

    return true;
}

bool InputRumble::findRumblePattern(const void* pOwner, s32* pExisting, s32* pEmpty, const RumblePattern& rPattern) {
    s32 emptyIndex = -1;

    for (s32 i = 0; i < mChannels.size(); i++) {
        const RumblePattern* pPattern = mChannels[i].mPattern;

        if (pPattern == nullptr) {
            if (emptyIndex == -1) {
                emptyIndex = i;
            }
        } else if (pPattern == &rPattern) {
            // Both are table entries, whose name hashes differ.
            *pExisting = i;
            return true;
        }
    }

    *pEmpty = emptyIndex;
    return false;
}
