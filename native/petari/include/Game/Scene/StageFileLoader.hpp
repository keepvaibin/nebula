#pragma once

#include "Game/Util/Array.hpp"


class StageFileLoader {
public:
    StageFileLoader(const char*);

    void startLoadingStageFile();
    void waitLoadedStageFile();
    void makeStageArchiveNameList();
    static void makeStageArchiveName(char*, u32, const char*);

    MR::GrowTable< char*, 0x18 > mStageFiles;
    s32 mZoneCount;
};
