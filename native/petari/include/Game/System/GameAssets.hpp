#pragma once

#include "Game/System/NebulaAtomic.hpp"

// The game's files: the asset store as the host serves it (asset_find, asset_child, asset_read). Paths from its root
// ("/StageData/EggStarGalaxy.arc"); a file's entry names it as the game's tables do.


class JKRHeap;

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

    // Which end of its heap a loaded file goes to.
    enum AllocDirection {
        ALLOC_DIRECTION_FORWARD = 1,
        ALLOC_DIRECTION_BACKWARD = 2,
    };

    // A read's result: the bytes read, or one of these.
    enum {
        cResultFailed = -1,
        cResultCanceled = -3,
    };

    struct Request;
    // Runs on the host's thread when the read ends, with the audio shut out.
    typedef void (*Callback)(s32 result, Request* pRequest);

    struct Request {
        Callback mCallback;
        void* mUser;
        NebulaAtomic< bool > mBusy;
        s32 mResult;  // when it has ended: the result its callback got
    };

    // The file or folder at pPath: its entry, or -1.
    s32 find(const char* pPath);
    bool open(const char* pPath, File* pFile);
    bool open(s32 entry, File* pFile);
    // The `index`-th entry of the folder at pPath, in the store's order: false past the last.
    bool folderEntry(const char* pPath, u32 index, FolderEntry* pEntry);

    // Reads `size` bytes at `offset` of the file: the calling thread sleeps while the host reads, the game's other threads run.
    s32 read(const File* pFile, void* pDst, s32 size, s32 offset);
    void readAsync(const File* pFile, Request* pRequest, void* pDst, s32 size, s32 offset, Callback callback);
    // A read cannot be withdrawn from the host: its callback gets cResultCanceled when it ends, and cancel() returns then.
    void cancel(Request* pRequest);

    // A whole file, or its first `maxLength` bytes when that is not 0, read into pDst, or into a new block of pHeap (the
    // current heap when null): null when there is no such file. A block ends at a 32-byte boundary, zeroed past the file.
    void* load(const char* pPath, u8* pDst, u32 maxLength, JKRHeap* pHeap, AllocDirection direction, u32* pSize);
    void* load(s32 entry, u8* pDst, u32 maxLength, JKRHeap* pHeap, AllocDirection direction, u32* pSize);
}  // namespace GameAssets
