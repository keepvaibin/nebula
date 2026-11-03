#pragma once

#include "Game/Util/SingletonHolder.hpp"
#include <JSystem/JAudio2/JAISound.hpp>

struct AudMeNameData {
    const char* mName;
    u32 mID;
    u32 mHash;
};

class AudMeNameConverter : public AudSingletonHolder< AudMeNameConverter > {
public:
    /// @brief Creates a new `AudMeNameConverter`.
    AudMeNameConverter();

    void init();
    u32 getMeID(const char*) const;

    u32 mNumEntries;
    AudMeNameData* mEntries;
};
