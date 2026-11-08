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

    // `game`: the game's own files under the mods' (GameAssets::findGame).
    s32 findIn(const char* pPath, bool game) {
        return game ? GameAssets::findGame(pPath) : GameAssets::find(pPath);
    }

    bool isIn(const char* pFilePath, bool considerLanguage, bool game);

    void languagePath(MR::FormattedName& rDst, const char* pFilePath, bool game) {
        MR::addFilePrefix(rDst, pFilePath, MR::getCurrentLanguagePrefix());

        if (!isIn(rDst.c_str(), false, game)) {
            rDst.format("%s", pFilePath);
        }
    }

    bool isIn(const char* pFilePath, bool considerLanguage, bool game) {
        if (!considerLanguage) {
            return findIn(pFilePath, game) >= 0;
        }

        MR::FormattedName filePath;
        languagePath(filePath, pFilePath, game);
        return findIn(filePath.c_str(), game) >= 0;
    }

    bool layoutPath(MR::FormattedName& rDst, const char* pFileName, bool game) {
        rDst.format("/Region/LayoutData/%s", pFileName);

        if (isIn(rDst.c_str(), false, game)) {
            return true;
        }

        rDst.format("/LayoutData/%s", pFileName);

        if (isIn(rDst.c_str(), true, game)) {
            return true;
        }

        rDst.format("%s", pFileName);

        return isIn(rDst.c_str(), false, game);
    }

    AssetFolder* archiveIn(const char* pFilePath, NebulaArena* pArena, bool game, NebulaArena** ppHeld) {
        MR::FormattedName filePath;
        languagePath(filePath, pFilePath, game);

        AssetFolder* pFolder = GameAssets::findArchive(filePath.c_str(), ppHeld, game);
        if (pFolder != nullptr) {
            return pFolder;
        }

        const s32 entry = findIn(filePath.c_str(), game);
        if (entry < 0) {
            return nullptr;
        }

        // With no mod's file over it, the game's own is the one held for everyone.
        if (game && GameAssets::find(filePath.c_str()) == entry) {
            return archiveIn(filePath.c_str(), pArena, false, ppHeld);
        }

        if (pArena == nullptr) {
            pArena = getHoldingArena();
        }

        pFolder = new (pArena, 0) AssetFolder(entry, GameAssets::load(entry, nullptr, 0, pArena, nullptr));
        GameAssets::holdArchive(filePath.c_str(), pFolder, pArena, game);
        if (ppHeld != nullptr) {
            *ppHeld = pArena;
        }
        return pFolder;
    }
}  // namespace

namespace MR {
    bool isFileExist(const char* pFilePath, bool considerLanguage) {
        return isIn(pFilePath, considerLanguage, false);
    }

    AssetFolder* getArchive(const char* pFilePath, NebulaArena* pArena) {
        return archiveIn(pFilePath, pArena, false, nullptr);
    }

    AssetFolder* getGameArchive(const char* pFilePath, NebulaArena** ppArena) {
        return archiveIn(pFilePath, nullptr, true, ppArena);
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
        languagePath(rDst, pFilePath, false);
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
        return layoutPath(rDst, pFileName, false);
    }

    bool makeGameLayoutArchiveFileName(FormattedName& rDst, const char* pFileName) {
        return layoutPath(rDst, pFileName, true);
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
