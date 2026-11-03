#pragma once

#include "Game/Util/SingletonHolder.hpp"
#include <JSystem/JAudio2/JAISound.hpp>

struct JAUSoundNameTable;

class AudSoundNameData {
public:
    AudSoundNameData();

    const char* mName;
    JAISoundID mID;
    u32 mHash;
};

class AudSoundNameConverter : public AudSingletonHolder< AudSoundNameConverter > {
public:
    AudSoundNameConverter();

    JAISoundID getSoundID(const char*) const;
    JAISoundID getSoundID(const char*, u32) const;
    void init();
    void initDataTable(const JAUSoundNameTable*);
    void addGroupInfo(const JAUSoundNameTable*, u8, u8);
    void addSectionInfo(const JAUSoundNameTable*, u8);
    u32 getSoundNameTableNumItem_inSection(const JAUSoundNameTable*, u8);

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
