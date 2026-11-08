#pragma once

#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/NameIndex.hpp"

// The speaker's sound names to their indices in the speaker table.
class CSSoundNameConverter : public AudSingletonHolder< CSSoundNameConverter > {
public:
    CSSoundNameConverter();

    void init();
    u32 getSoundID(const char*) const;

    MR::NameIndex mIndex;
};
