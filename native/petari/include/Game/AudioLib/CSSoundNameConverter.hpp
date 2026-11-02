#pragma once

#include "Game/Util/SingletonHolder.hpp"

class SpkTable;

struct CSSoundNameHashData {
    const char* mName;
    u32 mSoundID;
    s32 mHash;
};

class CSSoundNameConverter : public AudSingletonHolder< CSSoundNameConverter > {
public:
    /// @brief Creates a new `CSSoundNameConverter`.
    CSSoundNameConverter();

    void init();
    void initDataTable(const SpkTable*);
    void makeDataTable(const SpkTable*);
    u32 getSoundID(const char*) const;

    int mNumSounds;
    CSSoundNameHashData* mSoundHashes;
};
