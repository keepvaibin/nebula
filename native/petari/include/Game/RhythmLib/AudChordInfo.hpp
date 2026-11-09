#pragma once

#include "Game/Sound/SoundGlobal.hpp"

class AssetFolder;

// The game's side of the chord and scale the music names: the table it loads or drops, sent to the audio thread (ChordInfo), and
// whether a chord and scale are in use, as its own commands set it until the audio thread has run them.
class AudChordInfo : public SoundGlobal< AudChordInfo > {
public:
    AudChordInfo();

    void init(AssetFolder* pArchive);
    bool loadChordInfo(s16 id, bool keepCurrent);
    void invalidate();
    bool isAvailable() const;

private:
    s32 chordIndex() const;
    s32 scaleIndex() const;

    AssetFolder* mArchive;
    bool mAvailable;
    s32 mChordIndex;
    s32 mScaleIndex;
    u64 mSerial;
};
