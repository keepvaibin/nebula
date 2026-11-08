#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundGlobal.hpp"

struct SoundInfo : public SoundGlobal< SoundInfo > {
    SoundInfo(bool set);

    virtual int getSoundType(SoundID) const = 0;
    virtual int getCategory(SoundID) const = 0;
    virtual u32 getPriority(SoundID) const = 0;
    virtual void getSeInfo(SoundID, EffectSound*) const = 0;
    virtual void getSeqInfo(SoundID, SequenceSound*) const = 0;
    virtual void getStreamInfo(SoundID, StreamSound*) const = 0;
    virtual ~SoundInfo();
};
