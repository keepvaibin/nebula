#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "Game/System/GameThreads.hpp"

class JKRHeap;
class JKRSolidHeap;

struct JASDisposer {
    JASDisposer() {
    }
    virtual ~JASDisposer() {
    }
    virtual void onDispose() {
    }
};

#define DIR_MAX 64

struct JASWaveArcLoader {
    static JASHeap* getRootHeap();
    static void setCurrentDir(char const*);
    static char* getCurrentDir();

    static char sCurrentDir[DIR_MAX];
    static JASHeap* sSoundHeap;
};

struct JASWaveArc : JASDisposer {
    JASWaveArc();
    bool loadSetup(u32);
    bool eraseSetup();
    static void loadCallback(void*);
    bool sendLoadCmd();
    bool load(JASHeap*) ATTRIBUTE_WEAK; /* overwritten in OverwriteJAudio */
    bool loadTail(JASHeap*);
    bool erase();
    void setEntryNum(s32);
    void setFileName(char const*) ATTRIBUTE_WEAK; /* overwritten in OverwriteJAudio */

    virtual ~JASWaveArc();
    virtual void onDispose();
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
    JASHeap* getHeap() {
        return &mHeap;
    }

    struct loadCallbackParams {
        // not official struct name
        /* 0x0 */ JASWaveArc* mWavArc;
        /* 0x4 */ s32 mEntryNum;
        /* 0x8 */ u8* mBase;
        /* 0xC */ u32 _C;
    };

    /* 0x04 */ mutable JASHeap mHeap;
    /* 0x48 */ u32 _48;
    /* 0x4C */ NebulaAtomic< s32 > mStatus;
    /* 0x50 */ int mEntryNum;
    /* 0x54 */ u32 mFileLength;
    /* 0x58 */ u16 _58;
    /* 0x5A */ u16 _5A;
    /* 0x5C */ GameMutex mMutex;

    // Until an archive has finished loading since the last wait.
    static void waitLoadDone();
    static GameEvent sLoadDone;
};
