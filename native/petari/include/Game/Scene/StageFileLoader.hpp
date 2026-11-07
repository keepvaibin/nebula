#pragma once

#include "Game/Util/Array.hpp"

namespace MR {
    class FormattedName;
}

class StageFileLoader {
public:
    StageFileLoader(const char*);

    void loadStageFiles();
    void makeStageArchiveNameList();
    static void makeStageArchiveName(MR::FormattedName&, const char*);

    MR::GrowTable< char*, 0x18 > mStageFiles;
    s32 mZoneCount;
};
