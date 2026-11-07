#pragma once

#include <nebula_types.h>

// An effect's audible switch (the sound table's) in the high half of a word: which of its values the distance and
// position change.
struct SoundAudibleParam {
    SoundAudibleParam() {
    }

    SoundAudibleParam(u16 audibleSw, u16 low) : mValue(static_cast< u32 >(audibleSw) << 16 | low) {
    }

    SoundAudibleParam(u32 value) : mValue(value) {
    }

    operator u32() const {
        return mValue;
    }

    u16 getAudibleSw() const {
        return static_cast< u16 >(mValue >> 16);
    }

    u8 get_BIT11() const {
        return getAudibleSw() >> 11 & 1;
    }

    bool calcVolume() const {
        return getAudibleSw() >> 10 & 1;
    }

    bool calcFxMix() const {
        return getAudibleSw() >> 9 & 1;
    }

    u8 get_BIT8() const {
        return getAudibleSw() >> 8 & 1;
    }

    bool calcPan() const {
        return getAudibleSw() >> 7 & 1;
    }

    bool calcDolby() const {
        return getAudibleSw() >> 6 & 1;
    }

    bool calcDoppler() const {
        return getDoppler() != 0;
    }

    // The switch's top four bits.
    u32 getDoppler() const {
        return getAudibleSw() >> 12 & 0xf;
    }

    f32 getDopplerPower() const {
        return getDoppler() * (1.0f / 15.0f);
    }

    f32 getDopplerPower2() const {
        return getDoppler() * (1.0f / 15.0f);
    }

    u32 getVolDistBit() const {
        return 1 << ((getAudibleSw() >> 4) & 3);
    }

    u32 mValue;
};
