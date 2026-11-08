#pragma once

#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/NameIndex.hpp"
#include "Game/Sound/Sound.hpp"

class SoundNameTable;

// The game's sound names to their ids, from the sound name table.
class AudSoundNameConverter : public AudSingletonHolder< AudSoundNameConverter > {
public:
    AudSoundNameConverter();

    SoundID getSoundID(const char*) const;
    void init();

    MR::NameIndex mIndex;  // a name to its SoundID's value
};
