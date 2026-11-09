#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundGlobal.hpp"

// Each sound's values in the archive's sound table. One lives while the sound system does (the archive always has a table).
class StdSoundInfo : public SoundGlobal< StdSoundInfo > {
public:
    // An item's type: the top four bits of its mType.
    enum DataType {
        DATA_SE = 0x50,
        DATA_BGM = 0x60,
        DATA_STREAM = 0x70,
    };

    StdSoundInfo() : SoundGlobal< StdSoundInfo >(true) {
    }

    // 0 for a sound with no item.
    u32 getPriority(SoundID) const;
    // 0xffff for a sound that is not an effect.
    u16 getAudibleSw(SoundID) const;
    // 0xffff for a sound that is not music.
    u16 getBgmSeqResourceID(SoundID) const;
    // The store entry of a stream (or of a mod's track), -1 for none.
    s32 getStreamFileEntry(SoundID) const;

    // An effect's or a sequence's volume, as it starts.
    void applySound(SoundID, Sound*) const;
    // A stream's volume and its channels' pans, as it starts.
    void applyStream(SoundID, StreamSound*) const;

private:
    const char* getStreamFilePath(SoundID) const;
};
