#pragma once

#include "JSystem/JAudio2/JASBasicInst.hpp"

class NebulaArena;

const int PERC_MAX = 0x80;

struct JASDrumSet : public JASInst {
    struct TVelomap {
        s32 _0;
        s32 _4;
        f32 mVolume;
        f32 mPitch;
    };

    struct TPerc {
        TPerc();
        void setRelease(u32);

        void setVolume(f32 volume) {
            mVolume = volume;
        }
        void setPitch(f32 pitch) {
            mPitch = pitch;
        }
        void setPan(f32 pan) {
            mPan = pan;
        }

        void setVeloRegionCount(u32, NebulaArena*);
        TVelomap* getVeloRegion(int);
        const TVelomap* getVeloRegion(int) const;

        u32 getVeloRegionCount() const {
            return mVelomapCount;
        }

        void appendEffect(JASPtrListAdaptor< JASInstEffect >*);

        f32 mVolume;
        f32 mPitch;
        f32 mPan;
        u16 mRelease;
        u16 field_0xe;
        JASPtrList< JASInstEffect > mEffects;
        s32 mVelomapCount;
        TVelomap* mVelomap;
    };

    JASDrumSet();
    virtual ~JASDrumSet();
    void newPercArray(u8, NebulaArena*);
    virtual bool getParam(int, int, JASInstParam*) const;
    void setPerc(int, JASDrumSet::TPerc*);
    virtual u32 getType() const {
        return 'PERC';
    }

    TPerc* mPercArray[PERC_MAX];
     // u8 mPercNumMax;
};
