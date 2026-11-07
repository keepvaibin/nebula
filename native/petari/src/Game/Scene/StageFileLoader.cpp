#include "Game/Scene/StageFileLoader.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/Util.hpp"
#include "Game/System/AssetFolder.hpp"
#include <cstdio>
#include <cstring>
#include "Game/Util/StringUtil.hpp"

StageFileLoader::StageFileLoader(const char* pName) {
    mZoneCount = 0;
    makeStageArchiveNameList();
}

void StageFileLoader::loadStageFiles() {
    for (int i = 0; i < mZoneCount; i++) {
        MR::mountArchive(mStageFiles[i], MR::getSceneArchiveHeap());
    }
}

void StageFileLoader::makeStageArchiveNameList() {
    GalaxyStatusAccessor access = MR::makeCurrentGalaxyStatusAccessor();
    mZoneCount = access.getZoneNum();

    for (int i = 0; i < mZoneCount; i++) {
        const char* zoneName = access.getZoneName(i);
        MR::FormattedName path("/StageData/%s.arc", zoneName);
        u32 len = strlen(path.c_str()) + 1;
        mStageFiles.at(i) = new char[len];
        MR::copyString(mStageFiles[i], path.c_str(), len);
    }
}

void StageFileLoader::makeStageArchiveName(MR::FormattedName& rName, const char* pZoneName) {
    rName.format("/StageData/%s.arc", pZoneName);
}
