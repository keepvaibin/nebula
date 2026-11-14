#pragma once

#include "Game/System/GameAssets.hpp"



class AssetFolder;
class NebulaArena;

namespace MR {
    class FormattedName;

    /// @brief Determines if a file exists within the game's data.
    /// @param[in] pFilePath A pointer to the null-terminated absolute path of the file.
    /// @param[in] considerLanguage Determines if the file is localized.
    /// @return `true` if the file exists within the game's data, `false` otherwise.
    bool isFileExist(const char* pFilePath, bool considerLanguage);

    // The archive at pFilePath (the current language's when there is one), made on first use and held until the arena
    // holding it goes: pArena, or with none given, the asker's (the current) arena, except that what a stage's scene
    // arena asks for is held in the file cache arena (kept across a restart of that stage), and the file select's in
    // the permanent arena. Null when the store has no such archive.
    AssetFolder* getArchive(const char* pFilePath, NebulaArena* pArena = nullptr);

    // The game's own archive at pFilePath under any mod's, held apart from the mod's (in the asker's arena, as
    // getArchive); with no mod's over it, getArchive's. *ppArena, when asked, gets the arena holding it. Null when the
    // game has none.
    AssetFolder* getGameArchive(const char* pFilePath, NebulaArena** ppArena);

    // The arena holding pFilePath's archive; null when none is held.
    NebulaArena* getArchiveArena(const char* pFilePath);

    // Holds pFolder (an archive made from memory) under pFilePath until pArena goes.
    void addArchive(AssetFolder* pFolder, NebulaArena* pArena, const char* pFilePath);

    // Before the arena goes: the resource and layout holders made from its archives.
    void removeResourceHolders(NebulaArena* pArena);

    // Whether the store has the object or layout archive pName ("Kuribo", in ObjectData or LayoutData).
    bool isExistObjectOrLayoutArchive(const char* pName);

    /// @brief Creates an absolute path to a language-aware file.
    /// @param[out] rDst The path made.
    /// @param[in] pFilePath A pointer to the null-terminated absolute path of the file.
    void makeFileNameConsideringLanguage(FormattedName& rDst, const char* pFilePath);

    // The store path of the object archive pName ("Kuribo": the language's, ObjectData's or MapPartsData's, else pName
    // itself); whether the store has it. A mod's model "<id>:<path>" (references.md 4) is the archive its cook made of
    // assets/<path>.glb: /Luma/<id>/<path> (the store drops ".arc").
    bool makeObjectArchiveFileName(FormattedName& rDst, const char* pName);

    // The name of the model inside the object archive pName: pName for the game's ("Kuribo"); for a mod's
    // "<id>:<path>" the last segment of <path> (the cook's <leaf>.bmd, models.md 2). A pointer into pName.
    const char* getArchiveModelName(const char* pName);

    // The store path of the layout archive pName (the region's or LayoutData's, else pName itself); whether the store
    // has it.
    bool makeLayoutArchiveFileName(FormattedName& rDst, const char* pName);

    // As makeLayoutArchiveFileName, among the game's own files (under the mods').
    bool makeGameLayoutArchiveFileName(FormattedName& rDst, const char* pName);

    // The store path of layout pName's archive: its screen shape's (pName16x9 or pName4x3), else its replacement
    // (pNameReplace), else pName. Whether the store has one; with fallback, rDst names pName's even when it has none.
    bool makeLayoutArchiveFileNameFromPrefix(FormattedName& rDst, const char* pName, bool fallback);

    // The store path of a galaxy's scenario archive (StageData/<galaxy>Scenario).
    void makeScenarioArchiveFileName(FormattedName& rDst, const char* pGalaxyName);
};  // namespace MR
