#include "Game/Scene/StageFileLoader.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/Util.hpp"
#include "Game/System/AssetFolder.hpp"
#include <cstdio>
#include <cstring>

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
        char path[0x100];
        nebula_snprintf(path, sizeof(path), "/StageData/%s.arc", zoneName);
        u32 len = strlen(path) + 1;
        mStageFiles.at(i) = new char[len];
        MR::copyString(mStageFiles[i], path, len);
    }
}

void StageFileLoader::makeStageArchiveName(char* buf, u32 len, const char* pZoneName) {
    nebula_snprintf(buf, len, "/StageData/%s.arc", pZoneName);
}
