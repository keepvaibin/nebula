#include "JSystem/JUtility/JUTVideo.hpp"
#include "Game/System/NebulaKernel.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/System/NebulaFrameClock.hpp"

JUTVideo* JUTVideo::sManager;



JUTVideo* JUTVideo::createManager(u16 width, u16 height) {
    if (sManager == nullptr) {
        sManager = new JUTVideo(width, height);
    }
    return sManager;
}

void JUTVideo::destroyManager() {
    if (sManager != nullptr) {
        delete sManager;
        sManager = nullptr;
    }
}

JUTVideo::JUTVideo(u16 width, u16 height) {
    mWidth = width;
    mHeight = height;
    NebulaFrameClock::init();
    // Black for the first two frames shown.
    mSetBlack = true;
    mSetBlackFrameCount = 2;
    NebulaFrameClock::waitFrame();
    NebulaFrameClock::waitFrame();
    NebulaFrameClock::show(false);
    mPreRetraceCallback = NebulaFrameClock::setPreHook(preRetraceProc);
    mPostRetraceCallback = NebulaFrameClock::setPostHook(postRetraceProc);
    mPreCallback = nullptr;
    mPostCallback = nullptr;
    mMessageQueue.init(&mMessage, 1);
}

JUTVideo::~JUTVideo() {
    NebulaFrameClock::setPreHook(mPreRetraceCallback);
    NebulaFrameClock::setPostHook(mPostRetraceCallback);
}

void JUTVideo::preRetraceProc(u32 retrace_count) {
    if (sManager->mPreCallback) {
        (*sManager->mPreCallback)(retrace_count);
    }

    // The picture shown is the renderer's newest; black for the first retraces.
    if (sManager->mSetBlack == 1) {
        s32 frame_count = sManager->mSetBlackFrameCount;
        if (frame_count > 0) {
            frame_count--;
        }

        sManager->mSetBlackFrameCount = frame_count;
        sManager->mSetBlack = frame_count != 0;
        NebulaFrameClock::show(false);
        return;
    }
    NebulaFrameClock::show(true);
}

void JUTVideo::postRetraceProc(u32 retrace_count) {
    if (sManager->mPostCallback != nullptr) {
        sManager->mPostCallback(retrace_count);
    }

    void* message = reinterpret_cast< void* >(NebulaFrameClock::count());
    sManager->mMessageQueue.send(message, false);
}

void JUTVideo::waitRetraceIfNeed() {
}
