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
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        return GameAssets::find(filePath.c_str());
    }

    void* loadFile(const char* pFilePath, u8* pDst, NebulaArena* pHeap) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        return SingletonHolder< FileLoader >::get()->loadFile(filePath.c_str(), pDst, pHeap);
    }

    AssetFolder* mountArchive(const char* pFilePath, NebulaArena* pHeap) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        return SingletonHolder< FileLoader >::get()->mountArchive(filePath.c_str(), pHeap);
    }

    void mountArchiveByObjectOrLayoutName(const char* pFilePrefix, NebulaArena* pHeap) {
        NebulaArena* pLocalHeap;

        if (pHeap != nullptr) {
            pLocalHeap = pHeap;
        } else {
            pLocalHeap = getSceneArchiveHeap();
        }

        MR::FormattedName objArcPath;
        bool isExistObjArc = makeObjectArchiveFileNameFromPrefix(objArcPath, pFilePrefix, false);

        MR::FormattedName layoutArcPath;
        bool isExistLayoutArc = makeLayoutArchiveFileNameFromPrefix(layoutArcPath, pFilePrefix, false);

        if (isExistObjArc) {
            mountArchive(objArcPath.c_str(), pLocalHeap);
        } else if (isExistLayoutArc) {
            mountArchive(layoutArcPath.c_str(), pLocalHeap);
        }
    }

    void* getLoadedFile(const char* pFilePath) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        return SingletonHolder< FileLoader >::get()->getLoadedFile(filePath.c_str());
    }

    AssetFolder* getMountedArchive(const char* pFilePath) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        return SingletonHolder< FileLoader >::get()->getMountedArchive(filePath.c_str());
    }

    void createAndAddArchive(AssetFolder* pFolder, NebulaArena* pHeap, const char* pFilePath) {
        SingletonHolder< FileLoader >::get()->createAndAddArchive(pFolder, pHeap, pFilePath);
    }

    void getMountedArchiveAndHeap(const char* pFilePath, AssetFolder** ppArchive, NebulaArena** ppHeap) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        SingletonHolder< FileLoader >::get()->getMountedArchiveAndHeap(filePath.c_str(), ppArchive, ppHeap);
    }

    void removeFile(const char* pFilePath) {
        SingletonHolder< FileLoader >::get()->removeFile(pFilePath);
    }

    void removeFileConsideringLanguage(const char* pFilePath) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        SingletonHolder< FileLoader >::get()->removeFile(filePath.c_str());
    }

    void removeResourceAndFileHolderIfIsEqualHeap(NebulaArena* pHeap) {
        SingletonHolder< ResourceHolderManager >::get()->removeIfIsEqualHeap(pHeap);
        SingletonHolder< FileLoader >::get()->removeHolderIfIsEqualHeap(pHeap);
    }

    bool isLoadedFile(const char* pFilePath) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        return SingletonHolder< FileLoader >::get()->isLoaded(filePath.c_str());
    }

    bool isLoadedObjectOrLayoutArchive(const char* pFilePrefix) {
        MR::FormattedName objArcPath;
        bool isExistObjArc = makeObjectArchiveFileNameFromPrefix(objArcPath, pFilePrefix, false);

        MR::FormattedName layoutArcPath;
        makeLayoutArchiveFileNameFromPrefix(layoutArcPath, pFilePrefix, false);

        if (isExistObjArc) {
            return isLoadedFile(objArcPath.c_str());
        } else {
            return isLoadedFile(layoutArcPath.c_str());
        }
    }

    void makeFileNameConsideringLanguage(FormattedName& rDst, const char* pFilePath) {
        addFilePrefix(rDst, pFilePath, getCurrentLanguagePrefix());

        if (!isFileExist(rDst.c_str(), false)) {
            rDst.format("%s", pFilePath);
        }
    }

    bool makeObjectArchiveFileName(FormattedName& rDst, const char* pFileName) {
        rDst.format("/ObjectData/%s", pFileName);

        if (isFileExist(rDst.c_str(), true)) {
            return true;
        }

        rDst.format("/MapPartsData/%s", pFileName);

        if (isFileExist(rDst.c_str(), false)) {
            return true;
        }

        rDst.format("%s", pFileName);

        return isFileExist(rDst.c_str(), true);
    }

    bool makeObjectArchiveFileNameFromPrefix(FormattedName& rDst, const char* pFilePrefix, bool) {
        MR::FormattedName fileName("%s.arc", pFilePrefix);

        return makeObjectArchiveFileName(rDst, fileName.c_str());
    }

    bool makeLayoutArchiveFileName(FormattedName& rDst, const char* pFileName) {
        rDst.format("/Region/LayoutData/%s", pFileName);

        if (isFileExist(rDst.c_str(), false)) {
            return true;
        }

        rDst.format("/LayoutData/%s", pFileName);

        if (isFileExist(rDst.c_str(), true)) {
            return true;
        }

        rDst.format("%s", pFileName);

        return isFileExist(rDst.c_str(), false);
    }

    bool makeLayoutArchiveFileNameFromPrefix(FormattedName& rDst, const char* pFilePrefix, bool fallback) {
        MR::FormattedName fileName("%s.arc", pFilePrefix);
        bool isExistArc = makeLayoutArchiveFileName(rDst, fileName.c_str());

        const char* pAspectSuffix = isScreen16Per9() ? "16x9" : "4x3";
        fileName.format("%s%s.arc", pFilePrefix, pAspectSuffix);
        bool isExistAspectArc = makeLayoutArchiveFileName(rDst, fileName.c_str());

        fileName.format("%sReplace.arc", pFilePrefix);
        bool isExistReplaceArc = makeLayoutArchiveFileName(rDst, fileName.c_str());

        bool fileFound = isExistArc || isExistAspectArc || isExistReplaceArc;

        if (!fileFound && !fallback) {
            return false;
        }

        if (isExistAspectArc) {
            pAspectSuffix = isScreen16Per9() ? "16x9" : "4x3";
            fileName.format("%s%s.arc", pFilePrefix, pAspectSuffix);
        } else if (isExistReplaceArc) {
            fileName.format("%sReplace.arc", pFilePrefix);
        } else {
            fileName.format("%s.arc", pFilePrefix);
        }

        makeLayoutArchiveFileName(rDst, fileName.c_str());

        return true;
    }

    void makeScenarioArchiveFileName(FormattedName& rDst, const char* pStageName) {
        rDst.format("/StageData/%s/%sScenario.arc", pStageName, pStageName);
    }
};  // namespace MR
