#pragma once

// The game's files: the asset store as the host serves it (asset_find, asset_child, asset_read). Paths from its root
// ("/StageData/EggStarGalaxy", an archive's folder); a file's entry names it as the game's tables do.


class AssetFolder;
class NebulaArena;

namespace GameAssets {
    struct File {
        s32 mEntry;  // -1 when not open
        u32 mLength;
    };

    struct FolderEntry {
        s32 mEntry;
        bool mIsFolder;
        const char* mName;
    };

    // The file or folder at pPath: its entry, or -1.
    s32 find(const char* pPath);
    // The game's own file or folder at pPath under any mod's: its entry, or -1.
    s32 findGame(const char* pPath);
    bool open(const char* pPath, File* pFile);
    bool open(s32 entry, File* pFile);
    // The `index`-th entry of the folder at pPath, in the store's order: false past the last.
    bool folderEntry(const char* pPath, u32 index, FolderEntry* pEntry);

    // Reads `size` bytes at `offset` of the file, done when it returns.
    s32 read(const File* pFile, void* pDst, s32 size, s32 offset);
    // The file's bytes in the mapped store, read-only, for the process's life; may wait for the drive.
    const void* map(s32 entryNum);
    // Every file mapped for this arena: its views go with it, and the archives held for it.
    void unloadArena(const NebulaArena* pArena);
    // The arena a file view holding p was mapped for; null when p is in none.
    NebulaArena* viewArena(const void* p);

    // The archive held under pPath (letters in either case), or null; *ppArena, when asked, gets the arena holding it.
    // `game`: the game's own copy held apart from a mod's (getGameArchive).
    AssetFolder* findArchive(const char* pPath, NebulaArena** ppArena, bool game = false);
    // Holds pFolder under pPath until pArena goes.
    void holdArchive(const char* pPath, AssetFolder* pFolder, NebulaArena* pArena, bool game = false);

    // A whole file or folder (an archive: the bytes of its files, side by side), or its first `maxLength` bytes when that is not 0, read into pDst, or into a new block of pArena (the
    // current arena when null): null when there is no such file. A block has a zero past the file.
    void* load(const char* pPath, u8* pDst, u32 maxLength, NebulaArena* pArena, u32* pSize);
    void* load(s32 entry, u8* pDst, u32 maxLength, NebulaArena* pArena, u32* pSize);
}  // namespace GameAssets
