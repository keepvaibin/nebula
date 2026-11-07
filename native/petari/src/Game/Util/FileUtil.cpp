#include "Game/Util/FileUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/ResourceHolderManager.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <cstring>

namespace {
    NebulaArena* getHoldingArena() {
        const HeapMemoryWatcher* const pWatcher = SingletonHolder< HeapMemoryWatcher >::get();
        NebulaArena* const pCurrent = NebulaArena::getCurrentHeap();
        if (pWatcher->mSceneHeap == nullptr || pCurrent != pWatcher->mSceneHeap) {
            return pCurrent;
        }

        return MR::isStageFileSelect() ? pWatcher->mPermanent : pWatcher->mFileCacheHeap;
    }
}  // namespace

namespace MR {
    bool isFileExist(const char* pFilePath, bool considerLanguage) {
        if (!considerLanguage) {
            return GameAssets::find(pFilePath) >= 0;
        }

        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);
        return GameAssets::find(filePath.c_str()) >= 0;
    }

    AssetFolder* getArchive(const char* pFilePath, NebulaArena* pArena) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        AssetFolder* pFolder = GameAssets::findArchive(filePath.c_str(), nullptr);
        if (pFolder != nullptr) {
            return pFolder;
        }

        const s32 entry = GameAssets::find(filePath.c_str());
        if (entry < 0) {
            return nullptr;
        }

        if (pArena == nullptr) {
            pArena = getHoldingArena();
        }

        pFolder = new (pArena, 0) AssetFolder(entry, GameAssets::load(entry, nullptr, 0, pArena, nullptr));
        GameAssets::holdArchive(filePath.c_str(), pFolder, pArena);
        return pFolder;
    }

    NebulaArena* getArchiveArena(const char* pFilePath) {
        MR::FormattedName filePath;
        makeFileNameConsideringLanguage(filePath, pFilePath);

        NebulaArena* pArena = nullptr;
        GameAssets::findArchive(filePath.c_str(), &pArena);
        return pArena;
    }

    void addArchive(AssetFolder* pFolder, NebulaArena* pArena, const char* pFilePath) {
        GameAssets::holdArchive(pFilePath, pFolder, pArena);
    }

    void removeResourceHolders(NebulaArena* pArena) {
        SingletonHolder< ResourceHolderManager >::get()->removeIfIsEqualHeap(pArena);
    }

    bool isExistObjectOrLayoutArchive(const char* pFilePrefix) {
        MR::FormattedName path;
        return makeObjectArchiveFileNameFromPrefix(path, pFilePrefix, false) || makeLayoutArchiveFileNameFromPrefix(path, pFilePrefix, false);
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
