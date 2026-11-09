#include "Game/System/SaveDataFileAccessor.hpp"
#include "Game/Util/StringUtil.hpp"

SaveDataFileAccessor::SaveDataFileAccessor(u8* pSaveDataFile) : mFile(pSaveDataFile) {
}

void SaveDataFileAccessor::makeUserFileInfo(SaveDataUserFileInfo* pUserFileInfo, const char* pName) {
    pUserFileInfo->mData = nullptr;
    pUserFileInfo->mDataSize = 0;
    pUserFileInfo->mKind = SaveDataUserFileKind_Config;

    const u32 num = getUserFileInfoNum();

    for (u32 i = 0; i < num; i++) {
        const char* pEntryName = getFileName(i);

        if (!MR::isEqualString(pEntryName, pName)) {
            continue;
        }

        if (i == num - 1) {
            pUserFileInfo->mDataSize = getFileSize() - getFileOffset(i);
        } else {
            pUserFileInfo->mDataSize = getFileOffset(i + 1) - getFileOffset(i);
        }

        pUserFileInfo->mData = mFile + getFileOffset(i);

        if (MR::isEqualSubString(pEntryName, "mario") || MR::isEqualSubString(pEntryName, "luigi")) {
            pUserFileInfo->mKind = SaveDataUserFileKind_Game;
        }

        if (MR::isEqualSubString(pEntryName, "sysconf")) {
            pUserFileInfo->mKind = SaveDataUserFileKind_SysConf;
        }
    }
}
