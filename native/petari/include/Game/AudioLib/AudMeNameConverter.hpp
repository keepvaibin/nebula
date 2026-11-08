#pragma once

#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/NameIndex.hpp"

// The rhythm system's ME names to their indices in its parameter table.
class AudMeNameConverter : public AudSingletonHolder< AudMeNameConverter > {
public:
    AudMeNameConverter();

    void init();
    u32 getMeID(const char*) const;

    MR::NameIndex mIndex;
};
