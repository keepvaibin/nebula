#pragma once

#include "Game/System/SaveFileId.hpp"
#include "Game/System/SaveRequest.hpp"
#include <nebula_types.h>

class UserFile;

// The save: each user's files in memory, read from and written to one file through the host (save_request).
class SaveDataHandler {
public:
    SaveDataHandler(const UserFile*);

    // The save's file at `index`, the newest first, then its copies, then an old save's. False past the last.
    bool requestLoadSaveData(int index);
    // What the read found: taken into memory, or not usable.
    bool acceptLoaded();
    void initializeUserFileMemory(int, const UserFile*);
    void copyUserFileMemory(int, int);
    const u8* getFile(SaveFileId, u32* pSize) const;
    void storeUserFile(const UserFile*);
    void requestSaveSaveData();
    // Room for any one file the game makes.
    u32 getFileRoom() const { return mMakeRoom; }
    bool isDone();
    SaveRequest::Result getLastResult() const;

private:
    // Indexed by user, then kind.
    struct File {
        u8* mData;
        u32 mSize;
        u32 mRoom;
    };

    bool readNative();
    bool readOld();
    // Null for a user past the last.
    File* findFile(SaveFileId) const;
    void setFile(SaveFileId, const u8*, u32);

    SaveRequest* mSaveRequest;
    int mReadIndex;
    u32 mReadLength;
    u8* mBuffer;
    u32 mBufferRoom;
    File* mFiles;
    u8* mMake;
    u32 mMakeRoom;
};
