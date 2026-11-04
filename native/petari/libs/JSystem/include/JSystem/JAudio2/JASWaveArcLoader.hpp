#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "Game/System/GameThreads.hpp"

class NebulaArena;
class NebulaArena;

#define DIR_MAX 64

struct JASWaveArcLoader {
    static void setCurrentDir(char const*);
    static char* getCurrentDir();

    static char sCurrentDir[DIR_MAX];
};

struct JASWaveArc {
    JASWaveArc();
    bool loadSetup(u32);
    bool eraseSetup();
    bool sendLoadCmd();
    bool load(); /* overwritten in OverwriteJAudio */
    bool erase();
    void setEntryNum(s32);
    void setFileName(char const*); /* overwritten in OverwriteJAudio */

    virtual ~JASWaveArc();
    virtual void onLoadDone() {
    }
    virtual void onEraseDone() {
    }

    s32 getStatus() const {
        return mStatus;
    }
    u32 getFileSize() const {
        return mFileLength;
    }
    // The archive's waves, in the mapped store; null unless it is loaded.
    const u8* getData() const {
        return mStatus != 0 ? mData : nullptr;
    }

    struct loadCallbackParams {
        // not official struct name
        JASWaveArc* mWavArc;
        s32 mEntryNum;
        u32 _C;
    };

    u32 _48;
    NebulaAtomic< s32 > mStatus;
    int mEntryNum;
    u32 mFileLength;
    const u8* mData;
    u16 _58;
    GameMutex mMutex;

    // Until an archive has finished loading since the last wait.
};
