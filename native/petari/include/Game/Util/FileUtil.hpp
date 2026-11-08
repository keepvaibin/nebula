#pragma once

#include "Game/System/GameAssets.hpp"



class AssetFolder;
class NebulaArena;

namespace MR {
    class FormattedName;

    /// @brief Determines if a file exists within the disc file system.
    /// @param[in] pFilePath A pointer to the null-terminated absolute path of the file.
    /// @param[in] considerLanguage Determines if the file is localized.
    /// @return `true` if the file exists within the disc file system, `false` otherwise.
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

    // Whether the store has the object or layout archive pFilePrefix ("Kuribo": its .arc in ObjectData or LayoutData).
    bool isExistObjectOrLayoutArchive(const char* pFilePrefix);

    /// @brief Creates an absolute path to a language-aware file.
    /// @param[out] rDst The path made.
    /// @param[in] pFilePath A pointer to the null-terminated absolute path of the file.
    void makeFileNameConsideringLanguage(FormattedName& rDst, const char* pFilePath);

    /// @brief Creates an absolute path to the given object's archive file.
    /// @param[out] rDst The path made.
    /// @param[in] pFileName A pointer to the null-terminated name of the archive file.
    /// @return `true` if a valid archive file exists in the file system, `false` otherwise.
    bool makeObjectArchiveFileName(FormattedName& rDst, const char* pFileName);

    /// @brief Creates an absolute path to the given object's archive file.
    /// @param[out] rDst The path made.
    /// @param[in] pFilePrefix A pointer to the null-terminated name of the archive file, without the extension.
    /// @param[in] unused Unused flag.
    /// @return `true` if a valid archive file exists in the file system, `false` otherwise.
    bool makeObjectArchiveFileNameFromPrefix(FormattedName& rDst, const char* pFilePrefix, bool unused);

    /// @brief Creates an absolute path to the given layout's archive file.
    /// @param[out] rDst The path made.
    /// @param[in] pFileName A pointer to the null-terminated name of the archive file.
    /// @return `true` if a valid archive file exists in the file system, `false` otherwise.
    bool makeLayoutArchiveFileName(FormattedName& rDst, const char* pFileName);
    // As makeLayoutArchiveFileName, among the game's own files.
    bool makeGameLayoutArchiveFileName(FormattedName& rDst, const char* pFileName);

    /// @brief Creates an absolute path to the given layout's archive file.
    /// @param[out] rDst The path made.
    /// @param[in] pFilePrefix A pointer to the null-terminated name of the archive file, without the extension.
    /// @param[in] fallback Whether to fall back if none found.
    /// @return `true` if a valid archive file exists in the file system, `false` otherwise.
    bool makeLayoutArchiveFileNameFromPrefix(FormattedName& rDst, const char* pFilePrefix, bool fallback);

    /// @brief Creates an absolute path to the given galaxy's mission archive file.
    /// @param[out] rDst The path made.
    /// @param[in] pStageName A pointer to the null-terminated name of the galaxy.
    void makeScenarioArchiveFileName(FormattedName& rDst, const char* pStageName);
};  // namespace MR
