#include "JSystem/JUtility/JUTVideo.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>

JUTVideo* JUTVideo::sManager;

OSTick JUTVideo::sVideoLastTick;

OSTick JUTVideo::sVideoInterval;


JUTVideo* JUTVideo::createManager(_GXRenderModeObj const* param_0) {
    if (sManager == nullptr) {
        sManager = new JUTVideo(param_0);
    }
    return sManager;
}

void JUTVideo::destroyManager() {
    if (sManager != nullptr) {
        delete sManager;
        sManager = nullptr;
    }
}

JUTVideo::JUTVideo(GXRenderModeObj const* param_0) {
    mRenderObj = nullptr;
    VIInit();
    mSetBlack = true;
    mSetBlackFrameCount = 2;
    setRenderMode(param_0);
    VISetBlack(1);
    VIFlush();
    field_0x8 = 0;
    mRetraceCount = VIGetRetraceCount();
    field_0x10 = 1;
    field_0x18 = 0;
    sVideoLastTick = OSGetTick();
    sVideoInterval = 670000;
    mPreRetraceCallback = VISetPreRetraceCallback(preRetraceProc);
    mPostRetraceCallback = VISetPostRetraceCallback(postRetraceProc);
    mPreCallback = nullptr;
    mPostCallback = nullptr;
    OSInitMessageQueue(&mMessageQueue, &mMessage, 1);
}

JUTVideo::~JUTVideo() {
    VISetPreRetraceCallback(mPreRetraceCallback);
    VISetPostRetraceCallback(mPostRetraceCallback);
}

void JUTVideo::preRetraceProc(u32 retrace_count) {
    if (sManager->mPreCallback) {
        (*sManager->mPreCallback)(retrace_count);
    }

    OSTick tick = OSGetTick();
    sVideoInterval = tick - sVideoLastTick;
    sVideoLastTick = tick;

    // The picture shown is the renderer's newest; black for the first retraces.
    if (sManager->mSetBlack == 1) {
        s32 frame_count = sManager->mSetBlackFrameCount;
        if (frame_count > 0) {
            frame_count--;
        }

        sManager->mSetBlackFrameCount = frame_count;
        sManager->mSetBlack = frame_count != 0;
        VISetBlack(TRUE);
        VIFlush();
        return;
    }
    VISetBlack(FALSE);
    VIFlush();
}

void JUTVideo::postRetraceProc(u32 retrace_count) {
    if (sManager->mPostCallback != nullptr) {
        sManager->mPostCallback(retrace_count);
    }

    OSMessage message = reinterpret_cast< OSMessage >(VIGetRetraceCount());
    OSSendMessage(&sManager->mMessageQueue, message, OS_MESSAGE_NOBLOCK);
}

void JUTVideo::setRenderMode(GXRenderModeObj const* pObj) {
    if (mRenderObj != nullptr && pObj->viTVmode != mRenderObj->viTVmode) {
        mSetBlack = true;
        mSetBlackFrameCount = 4;
    }
    mRenderObj = const_cast< GXRenderModeObj* >(pObj);
    VIFlush();

    if (mSetBlack) {
        VIWaitForRetrace();
        VIWaitForRetrace();
    }
}

void JUTVideo::waitRetraceIfNeed() {
}
