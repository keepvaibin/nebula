#pragma once

// The game's files: the asset store as the host serves it (asset_find, asset_child, asset_read). Paths from its root
// ("/StageData/EggStarGalaxy.arc"); a file's entry names it as the game's tables do.


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
    bool open(const char* pPath, File* pFile);
    bool open(s32 entry, File* pFile);
    // The `index`-th entry of the folder at pPath, in the store's order: false past the last.
    bool folderEntry(const char* pPath, u32 index, FolderEntry* pEntry);

    // Reads `size` bytes at `offset` of the file: the calling thread sleeps while the host reads, the game's other threads run.
    s32 read(const File* pFile, void* pDst, s32 size, s32 offset);
    // The file's bytes in the mapped store, read-only, for the process's life; may wait for the drive.
    const void* map(s32 entryNum);
    // Every file mapped for this arena: its views go with it.
    void unloadHeap(const NebulaArena* pHeap);

    // A whole file or folder (an archive: the bytes of its files, side by side), or its first `maxLength` bytes when that is not 0, read into pDst, or into a new block of pHeap (the
    // current heap when null): null when there is no such file. A block has a zero past the file.
    void* load(const char* pPath, u8* pDst, u32 maxLength, NebulaArena* pHeap, u32* pSize);
    void* load(s32 entry, u8* pDst, u32 maxLength, NebulaArena* pHeap, u32* pSize);
}  // namespace GameAssets
