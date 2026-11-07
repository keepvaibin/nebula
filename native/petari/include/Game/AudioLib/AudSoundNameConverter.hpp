#pragma once

#include "Game/Util/SingletonHolder.hpp"
#include "Game/Sound/Sound.hpp"

class SoundNameTable;

class AudSoundNameData {
public:
    AudSoundNameData();

    const char* mName;
    SoundID mID;
    u32 mHash;
};

class AudSoundNameConverter : public AudSingletonHolder< AudSoundNameConverter > {
public:
    AudSoundNameConverter();

    SoundID getSoundID(const char*) const;
    SoundID getSoundID(const char*, u32) const;
    void init();
    void initDataTable(const SoundNameTable*);
    void addGroupInfo(const SoundNameTable*, u8, u8);
    void addSectionInfo(const SoundNameTable*, u8);
    u32 getSoundNameTableNumItem_inSection(const SoundNameTable*, u8);

    inline s32 getSeSoundCategory(const char*) const;
    inline s32 getOtherSoundCategory(const char*) const;

    s32 mNumItems;
    s32 mNumNameDatas;
    u32* mGroupItemOffsets;
    s32 mNumItemsSection0;
    s32 mNumItemsSection1;
    s32 mNumItemsSection2;
    AudSoundNameData* mSoundNameData;
};
