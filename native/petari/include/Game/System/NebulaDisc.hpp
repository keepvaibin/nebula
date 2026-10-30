#pragma once

// The game's disc (B-12): its files and folders as the host serves them (disc_find, disc_child, disc_read), in place
// of the console's DVD library. Paths are the disc's ("/StageData/EggStarGalaxy.arc"); a file's entry (its index in
// the disc's file table) names it as the game's tables do.

#include <revolution/types.h>

namespace NebulaDisc {
    struct File {
        s32 mEntry;  // -1 when not open
        u32 mLength;
    };

    struct FolderEntry {
        s32 mEntry;
        bool mIsFolder;
        const char* mName;
    };

    // A read's result: the bytes read, or one of these.
    enum {
        cResultFailed = -1,
        cResultCanceled = -3,
    };

    struct Request;
    // Runs on the game's thread when the read ends (the console's disc callback).
    typedef void (*Callback)(s32 result, Request* pRequest);

    struct Request {
        Callback mCallback;
        void* mUser;
        volatile bool mBusy;
        s32 mResult;  // when it has ended: the result its callback got
    };

    void init();
    // The file or folder at pPath: its entry, or -1.
    s32 find(const char* pPath);
    bool open(const char* pPath, File* pFile);
    bool open(s32 entry, File* pFile);
    // The `index`-th entry of the folder at pPath, in the disc's order: false past the last.
    bool folderEntry(const char* pPath, u32 index, FolderEntry* pEntry);

    // Reads `size` bytes at `offset` of the file (the end may run up to 31 bytes past it): the calling thread sleeps
    // while the host reads, the game's other threads run.
    s32 read(const File* pFile, void* pDst, s32 size, s32 offset);
    bool readAsync(const File* pFile, Request* pRequest, void* pDst, s32 size, s32 offset, Callback callback);
    // A read cannot be withdrawn from the host: its callback gets cResultCanceled when it ends, and cancel() returns
    // then.
    void cancel(Request* pRequest);
    // A read in flight.
    bool isBusy();
}  // namespace NebulaDisc
