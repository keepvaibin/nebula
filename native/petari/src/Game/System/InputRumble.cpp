#include "Game/System/InputRumble.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/InputRumbleData.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"

InputRumble** InputRumble::sInstanceForCallback;

void RumbleChannel::clear() {
    _0 = nullptr;
    _8 = 0;
    _C = 0;
    mStrength = 0;
    _4 = false;
    _10 = nullptr;
}

void RumbleChannel::update() {
    if (_0 == nullptr) {
        return;
    }

    if (_C >= _0->mFrame) {
        if (!_4) {
            clear();
            return;
        }

        _C = 0;
    }

    // How much of the frames around this one the pattern fills: its pulses are one soft buzz.
    const s32 cReach = 2;
    s32 on = 0;
    for (s32 frame = _C - cReach; frame <= _C + cReach; frame++) {
        s32 place = frame;
        if (place < 0 || place >= _0->mFrame) {
            if (!_4) {
                continue;
            }
            place = (place + _0->mFrame) % _0->mFrame;
        }

        on += _0->mPattern[place];
    }

    mStrength = static_cast< u8 >(_0->mPattern[_C] != 0 ? 255 * (on + 1) / (2 * cReach + 2) : 255 * on / (2 * cReach + 2));
    _C++;
}

void RumbleChannel::setPattern(const void* pParam1, const RumblePattern& rParam2, u32 param3, bool param4) {
    _8 = param3;
    _C = 0;
    _0 = &rParam2;
    _10 = pParam1;
    _4 = param4;
}

InputRumble::InputRumble(PlayerInput* pPad) : mPad(pPad), mStrength(), _C(1), _BC() {
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
            isActive |= mChannel[i]._0 != nullptr;
        }
        if (isActive) {
            stop();
        }
        return;
    }

    if (_BC > 0) {
        _BC--;

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
        _C = (_C + 1) & 0x7FFFFFFF;

        mChannel[v2].setPattern(pParam1, rParam2, _C, param3);

        return true;
    }

    _C = (_C + 1) & 0x7FFFFFFF;

    mChannel[v3].setPattern(pParam1, rParam2, _C, param3);

    return true;
}

bool InputRumble::findRubmlePattern(const void* pOwner, s32* pExisting, s32* pEmpty, s32* pOldest, const RumblePattern& rPattern) {
    u32 oldest = -1;
    u8 oldestIndex = 0xFF;
    u8 emptyIndex = 0xFF;

    for (u8 i = 0; i < ARRAY_SIZE(mChannel); i++) {
        const RumblePattern* pPattern = mChannel[i]._0;

        if (pPattern == nullptr) {
            if (emptyIndex == 0xFF) {
                emptyIndex = i;
            }
        } else {
            if (pPattern->mHash == rPattern.mHash) {
                *pExisting = i;
                return true;
            }

            if (oldest > mChannel[i]._8) {
                oldest = mChannel[i]._8;
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
