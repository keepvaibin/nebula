#include "Game/Util/FileUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/ResourceHolderManager.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"

namespace {
    NebulaArena* getHoldingArena() {
        NebulaArena* const pCurrent = NebulaArena::current();
        if (Arenas::scene() == nullptr || pCurrent != Arenas::scene()) {
            return pCurrent;
        }

        return MR::isStageFileSelect() ? Arenas::permanent() : Arenas::sceneFiles();
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
        SingletonHolder< ResourceHolderManager >::get()->removeArenaHolders(pArena);
    }

    bool isExistObjectOrLayoutArchive(const char* pName) {
        MR::FormattedName path;
        return makeObjectArchiveFileName(path, pName) || makeLayoutArchiveFileNameFromPrefix(path, pName, false);
    }

    void makeFileNameConsideringLanguage(FormattedName& rDst, const char* pFilePath) {
        languagePath(rDst, pFilePath, false);
    }

    bool makeObjectArchiveFileName(FormattedName& rDst, const char* pName) {
        // A mod's model (references 4): no game archive name has a ':'.
        if (const char* const pColon = strchr(pName, ':'); pColon != nullptr) {
            char id[128];
            const u32 length = static_cast< u32 >(pColon - pName);
            if (length == 0 || length >= sizeof(id) || pColon[1] == 0) {
                rDst.format("%s", pName);
                return false;
            }
            memcpy(id, pName, length);
            id[length] = 0;
            rDst.format("/Luma/%s/%s", id, pColon + 1);
            return isFileExist(rDst.c_str(), false);
        }

        rDst.format("/ObjectData/%s", pName);

        if (isFileExist(rDst.c_str(), true)) {
            return true;
        }

        rDst.format("/MapPartsData/%s", pName);

        if (isFileExist(rDst.c_str(), false)) {
            return true;
        }

        rDst.format("%s", pName);

        return isFileExist(rDst.c_str(), true);
    }

    const char* getArchiveModelName(const char* pName) {
        const char* const pColon = strchr(pName, ':');
        if (pColon == nullptr) {
            return pName;
        }
        const char* pLeaf = pColon + 1;
        for (const char* p = pLeaf; *p != 0; p++) {
            if (*p == '/') {
                pLeaf = p + 1;
            }
        }
        return pLeaf;
    }

    bool makeLayoutArchiveFileName(FormattedName& rDst, const char* pName) {
        return layoutPath(rDst, pName, false);
    }

    bool makeGameLayoutArchiveFileName(FormattedName& rDst, const char* pName) {
        return layoutPath(rDst, pName, true);
    }

    bool makeLayoutArchiveFileNameFromPrefix(FormattedName& rDst, const char* pName, bool fallback) {
        MR::FormattedName name("%s%s", pName, isScreen16Per9() ? "16x9" : "4x3");
        if (makeLayoutArchiveFileName(rDst, name.c_str())) {
            return true;
        }

        name.format("%sReplace", pName);
        if (makeLayoutArchiveFileName(rDst, name.c_str())) {
            return true;
        }

        return makeLayoutArchiveFileName(rDst, pName) || fallback;
    }

    void makeScenarioArchiveFileName(FormattedName& rDst, const char* pGalaxyName) {
        rDst.format("/StageData/%sScenario", pGalaxyName);
    }
};  // namespace MR
