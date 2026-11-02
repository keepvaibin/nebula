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
    static const u32 cAllocSize = 32;
    bool load(JASHeap*); /* overwritten in OverwriteJAudio */
    bool loadTail(JASHeap*);
    bool erase();
    void setEntryNum(s32);
    void setFileName(char const*); /* overwritten in OverwriteJAudio */

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
    // The archive's waves, in the mapped store; null unless it is loaded.
    const u8* getData() const {
        return mHeap.getBase() != nullptr ? mData : nullptr;
    }

    struct loadCallbackParams {
        // not official struct name
        /* 0x0 */ JASWaveArc* mWavArc;
        /* 0x4 */ s32 mEntryNum;
        /* 0xC */ u32 _C;
    };

    /* 0x04 */ mutable JASHeap mHeap;  // no bytes to speak of (cAllocSize): an archive's place in its section, which goes with it
    /* 0x48 */ u32 _48;
    /* 0x4C */ NebulaAtomic< s32 > mStatus;
    /* 0x50 */ int mEntryNum;
    /* 0x54 */ u32 mFileLength;
    const u8* mData;
    /* 0x58 */ u16 _58;
    /* 0x5A */ u16 _5A;
    /* 0x5C */ GameMutex mMutex;

    // Until an archive has finished loading since the last wait.
    static void waitLoadDone();
    static GameEvent sLoadDone;
};
