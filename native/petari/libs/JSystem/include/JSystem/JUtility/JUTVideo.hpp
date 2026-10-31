#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include "Game/System/NebulaKernel.hpp"
#include "Game/System/NebulaFrameClock.hpp"

// The game's frame: the size it draws in (its viewport's units), and the hooks around each shown picture.
class JUTVideo {
public:
    typedef void (*Callback)(u32);

    JUTVideo(u16 width, u16 height);
    virtual ~JUTVideo();

    static JUTVideo* createManager(u16 width, u16 height);
    static void destroyManager();
    void waitRetraceIfNeed();

    static void preRetraceProc(u32);
    static void postRetraceProc(u32);

    u16 getFbWidth() const {
        return mWidth;
    }
    u16 getEfbHeight() const {
        return mHeight;
    }
    void getBounds(u16& width, u16& height) const {
        width = (u16)getFbWidth();
        height = (u16)getEfbHeight();
    }
    NebulaMessageQueue* getMessageQueue() {
        return &mMessageQueue;
    }

    static JUTVideo* getManager() {
        return sManager;
    }

private:
    static JUTVideo* sManager;

private:
    u16 mWidth;
    u16 mHeight;
    /* 0x1C */ NebulaFrameClock::Hook mPreRetraceCallback;
    /* 0x20 */ NebulaFrameClock::Hook mPostRetraceCallback;
    /* 0x24 */ Callback mPreCallback;
    /* 0x28 */ Callback mPostCallback;
    /* 0x2C */ bool mSetBlack;
    /* 0x30 */ s32 mSetBlackFrameCount;
    /* 0x34 */ void* mMessage;
    /* 0x38 */ NebulaMessageQueue mMessageQueue;
};

inline JUTVideo* JUTGetVideoManager() {
    return JUTVideo::getManager();
}

inline void JUTDestroyVideoManager() {
    JUTVideo::destroyManager();
}
