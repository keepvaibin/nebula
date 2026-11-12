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
    // pSrc's game data and config into pDst, as a save and a load of them would (the backup the sequence keeps).
    void copyUserFile(UserFile* pDst, const UserFile* pSrc);
    bool isDone();
    SaveRequest::Result getLastResult() const;
    // FORGET (services-save 5): `pId`'s values out of each game-data file in memory (written by the next save).
    void forgetLumaMod(const char* pId);

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
    // mMake made big enough for pUserFile's game data: the fixed part's room and its mods' values (services-save).
    void fitMake(const UserFile* pUserFile);

    SaveRequest* mSaveRequest;
    int mReadIndex;
    u32 mReadLength;
    u8* mBuffer;
    u32 mBufferRoom;
    File* mFiles;
    u8* mMake;
    u32 mMakeRoom;
    // The room a file takes without mods' values, and room beside it.
    u32 mBaseRoom;
};
