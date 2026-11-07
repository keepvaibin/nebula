#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundInfo.hpp"
#include "Game/Sound/StreamSource.hpp"
#include "Game/Sound/SoundTableInfo.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class StdSoundInfo : public SoundInfo, public SoundTableInfo, public StreamSource {
public:
    StdSoundInfo(bool b) : SoundInfo(b), SoundTableInfo(b), StreamSource() {};

    virtual int getSoundType(SoundID) const;
    virtual int getCategory(SoundID) const;
    virtual u32 getPriority(SoundID) const;
    virtual void getSeInfo(SoundID, EffectSound*) const;
    virtual void getSeqInfo(SoundID, SequenceSound*) const;
    virtual void getStreamInfo(SoundID, StreamSound*) const;
    virtual ~StdSoundInfo() {
    }

    virtual u16 getAudibleSw(SoundID) const;
    virtual u16 getBgmSeqResourceID(SoundID) const;
    virtual s32 getStreamFileEntry(SoundID);

    void getSoundInfo_(SoundID, Sound*) const;
    const char* getStreamFilePath(SoundID);
};
