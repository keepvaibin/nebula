#include "Game/Util/FileUtil.hpp"
#include "Game/System/FileLoader.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/ResourceHolderManager.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <cstdio>
#include <cstring>
#include "Game/System/GameAssets.hpp"

namespace MR {
    bool isFileExist(const char* pFilePath, bool considerLanguage) {
        s32 entryNum;

        if (considerLanguage) {
            entryNum = convertPathToEntrynumConsideringLanguage(pFilePath);
        } else {
            entryNum = GameAssets::find(pFilePath);
        }

        return entryNum >= 0;
    }

    s32 convertPathToEntrynumConsideringLanguage(const char* pFilePath) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        return GameAssets::find(filePath);
    }

    void* loadToMainRAM(const char* pFilePath, u8* pDst, NebulaArena* pHeap, GameAssets::AllocDirection allocDir) {
        loadAsyncToMainRAM(pFilePath, pDst, pHeap, allocDir);

        return receiveFile(pFilePath);
    }

    void loadAsyncToMainRAM(const char* pFilePath, u8* pDst, NebulaArena* pHeap, GameAssets::AllocDirection allocDir) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        SingletonHolder< FileLoader >::get()->requestLoadToMainRAM(filePath, pDst, pHeap, allocDir, false);
    }

    AssetFolder* mountArchive(const char* pFilePath, NebulaArena* pHeap) {
        mountAsyncArchive(pFilePath, pHeap);

        return receiveArchive(pFilePath);
    }

    void mountAsyncArchive(const char* pFilePath, NebulaArena* pHeap) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        SingletonHolder< FileLoader >::get()->requestMountArchive(filePath, pHeap, false);
    }

    void mountAsyncArchiveByObjectOrLayoutName(const char* pFilePrefix, NebulaArena* pHeap) {
        NebulaArena* pLocalHeap;

        if (pHeap != nullptr) {
            pLocalHeap = pHeap;
        } else {
            pLocalHeap = getAproposHeapForSceneArchive(0.03f);
        }

        char objArcPath[256];
        bool isExistObjArc = makeObjectArchiveFileNameFromPrefix(objArcPath, sizeof(objArcPath), pFilePrefix, false);

        char layoutArcPath[256];
        bool isExistLayoutArc = makeLayoutArchiveFileNameFromPrefix(layoutArcPath, sizeof(layoutArcPath), pFilePrefix, false);

        if (isExistObjArc) {
            mountAsyncArchive(objArcPath, pLocalHeap);
        } else if (isExistLayoutArc) {
            mountAsyncArchive(layoutArcPath, pLocalHeap);
        }
    }

    void* receiveFile(const char* pFilePath) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        return SingletonHolder< FileLoader >::get()->receiveFile(filePath);
    }

    AssetFolder* receiveArchive(const char* pFilePath) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        return SingletonHolder< FileLoader >::get()->receiveArchive(filePath);
    }

    void receiveAllRequestedFile() {
        SingletonHolder< FileLoader >::get()->receiveAllRequestedFile();
    }

    void createAndAddArchive(AssetFolder* pFolder, NebulaArena* pHeap, const char* pFilePath) {
        SingletonHolder< FileLoader >::get()->createAndAddArchive(pFolder, pHeap, pFilePath);
    }

    void getMountedArchiveAndHeap(const char* pFilePath, AssetFolder** ppArchive, NebulaArena** ppHeap) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        SingletonHolder< FileLoader >::get()->getMountedArchiveAndHeap(filePath, ppArchive, ppHeap);
    }

    void removeFile(const char* pFilePath) {
        SingletonHolder< FileLoader >::get()->removeFile(pFilePath);
    }

    void removeFileConsideringLanguage(const char* pFilePath) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        SingletonHolder< FileLoader >::get()->removeFile(filePath);
    }

    void removeResourceAndFileHolderIfIsEqualHeap(NebulaArena* pHeap) {
        SingletonHolder< ResourceHolderManager >::get()->removeIfIsEqualHeap(pHeap);
        SingletonHolder< FileLoader >::get()->removeHolderIfIsEqualHeap(pHeap);
    }

    bool isLoadedFile(const char* pFilePath) {
        char filePath[256];
        makeFileNameConsideringLanguage(filePath, sizeof(filePath), pFilePath);

        return SingletonHolder< FileLoader >::get()->isLoaded(filePath);
    }

    bool isLoadedObjectOrLayoutArchive(const char* pFilePrefix) {
        char objArcPath[256];
        bool isExistObjArc = makeObjectArchiveFileNameFromPrefix(objArcPath, sizeof(objArcPath), pFilePrefix, false);

        char layoutArcPath[256];
        makeLayoutArchiveFileNameFromPrefix(layoutArcPath, sizeof(layoutArcPath), pFilePrefix, false);

        if (isExistObjArc) {
            return isLoadedFile(objArcPath);
        } else {
            return isLoadedFile(layoutArcPath);
        }
    }

    void makeFileNameConsideringLanguage(char* pDst, u32 size, const char* pFilePath) {
        addFilePrefix(pDst, size, pFilePath, getCurrentLanguagePrefix());

        if (!isFileExist(pDst, false)) {
            nebula_snprintf(pDst, size, "%s", pFilePath);
        }
    }

    bool makeObjectArchiveFileName(char* pDst, u32 size, const char* pFileName) {
        nebula_snprintf(pDst, size, "/ObjectData/%s", pFileName);

        if (isFileExist(pDst, true)) {
            return true;
        }

        nebula_snprintf(pDst, size, "/MapPartsData/%s", pFileName);

        if (isFileExist(pDst, false)) {
            return true;
        }

        nebula_snprintf(pDst, size, "%s", pFileName);

        return isFileExist(pDst, true);
    }

    bool makeObjectArchiveFileNameFromPrefix(char* pDst, u32 size, const char* pFilePrefix, bool) {
        char fileName[256];
        nebula_snprintf(fileName, sizeof(fileName), "%s.arc", pFilePrefix);

        return makeObjectArchiveFileName(pDst, size, fileName);
    }

    bool makeLayoutArchiveFileName(char* pDst, u32 size, const char* pFileName) {
        nebula_snprintf(pDst, size, "/Region/LayoutData/%s", pFileName);

        if (isFileExist(pDst, false)) {
            return true;
        }

        nebula_snprintf(pDst, size, "/LayoutData/%s", pFileName);

        if (isFileExist(pDst, true)) {
            return true;
        }

        nebula_snprintf(pDst, size, "%s", pFileName);

        return isFileExist(pDst, false);
    }

    bool makeLayoutArchiveFileNameFromPrefix(char* pDst, u32 size, const char* pFilePrefix, bool fallback) {
        char fileName[256];
        nebula_snprintf(fileName, sizeof(fileName), "%s.arc", pFilePrefix);
        bool isExistArc = makeLayoutArchiveFileName(pDst, size, fileName);

        const char* pAspectSuffix = isScreen16Per9() ? "16x9" : "4x3";
        nebula_snprintf(fileName, sizeof(fileName), "%s%s.arc", pFilePrefix, pAspectSuffix);
        bool isExistAspectArc = makeLayoutArchiveFileName(pDst, size, fileName);

        nebula_snprintf(fileName, sizeof(fileName), "%sReplace.arc", pFilePrefix);
        bool isExistReplaceArc = makeLayoutArchiveFileName(pDst, size, fileName);

        bool fileFound = isExistArc || isExistAspectArc || isExistReplaceArc;

        if (!fileFound && !fallback) {
            return false;
        }

        if (isExistAspectArc) {
            pAspectSuffix = isScreen16Per9() ? "16x9" : "4x3";
            nebula_snprintf(fileName, sizeof(fileName), "%s%s.arc", pFilePrefix, pAspectSuffix);
        } else if (isExistReplaceArc) {
            nebula_snprintf(fileName, sizeof(fileName), "%sReplace.arc", pFilePrefix);
        } else {
            nebula_snprintf(fileName, sizeof(fileName), "%s.arc", pFilePrefix);
        }

        makeLayoutArchiveFileName(pDst, size, fileName);

        return true;
    }

    void makeScenarioArchiveFileName(char* pDst, u32 size, const char* pStageName) {
        nebula_snprintf(pDst, size, "/StageData/%s/%sScenario.arc", pStageName, pStageName);
    }
};  // namespace MR
