#pragma once

#include "Game/System/NebulaAtomic.hpp"

// One request on the game's save files through the host's save service (nebula_game_api.h save_request): set up by
// one of the set* calls, started by start(), done when the host has finished it (the game polls isDone). The host
// writes the status from its own thread.
class SaveRequest {
public:
    // What became of a request: the host's save statuses.
    enum Result {
        Result_Done,       // read or written
        Result_Pending,    // still with the host
        Result_NoFile,     // a read found no file
        Result_Corrupted,  // the file is larger than the game's save (not this game's save data)
        Result_Failed,     // the host could not read or write it
    };

    SaveRequest();

    void init();
    bool isDone() const;
    // The result, once done.
    Result getResult() const;
    void start();
    // The name is held, not copied: it must last until the request is done.
    const char* setWriteSeq(const char*, const void*, u32);
    const char* setReadSeq(const char*, void*, u32, u32*);

private:
    enum Type {
        Type_None,
        Type_Write,
        Type_Read,
    };

    const char* mName;
    Type mType;
    void* mBuffer;
    u32 mSize;           // a write's length, a read's capacity
    u32* mReadLength;    // a read: the file's length
    NebulaAtomic< s32 > mStatus;  // NEBULA_SAVE_*; the host's save worker stores the result
};
