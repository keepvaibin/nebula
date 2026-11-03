#pragma once

#include "JSystem/JAudio2/JASHeapCtrl.hpp"

struct JAUAudibleParam {
    JAUAudibleParam() {
    }

    JAUAudibleParam(u16 a1, u16 a2) {
        mAudibleSw = a1;
        _f1 = a2;
    }

    JAUAudibleParam(u32 id) {
        raw = id;
    }

    operator u32() const {
        return raw;
    }

    u16 getAudibleSw() const {
        return mAudibleSw;
    }

    u8 get_BIT11() const {
        return mAudibleSw >> 11 & 1;
    }

    bool calcVolume() const {
        return mAudibleSw >> 10 & 1;
    }

    bool calcFxMix() const {
        return mAudibleSw >> 9 & 1;
    }

    u8 get_BIT8() const {
        return mAudibleSw >> 8 & 1;
    }

    bool calcPan() const {
        return mAudibleSw >> 7 & 1;
    }

    bool calcDolby() const {
        return mAudibleSw >> 6 & 1;
    }

    bool calcDoppler() const {
        return (raw >> 28 & 0xF) != 0;
    }

    u32 getDoppler() const {
        // The top nibble of mAudibleSw.
        return (u32)((mAudibleSw >> 12) & 0xf);
        // return raw >> 28 & 0xF;
    }

    f32 getDopplerPower() const {
        JAUAudibleParam param(*this);
        return param.getDoppler() * (1.0f / 15.0f);
    }

    f32 getDopplerPower2() const {
        JAUAudibleParam param(*this);
        return param.getDoppler() * (1.0f / 15.0f);
    }

    u32 getVolDistBit() const {
        return 1 << ((mAudibleSw >> 4) & 3);
    }

    // raw holds mAudibleSw in its high half.
    union {
        struct {
            u16 _f1;
            u16 mAudibleSw;
        };
        u32 raw;
    };
};

// TODO: these are probably what AudAudible inherits from in AudioLib
template < int SIZE >
class JAUAudible : public JASPoolAllocObject< JAUAudible< SIZE > > {
public:
    JAUAudible() {};
};

template < int SIZE >
class JAUDopplerAudible : public JASPoolAllocObject< JAUDopplerAudible< SIZE > > {
public:
    JAUDopplerAudible() {};
};
