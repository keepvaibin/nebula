#include "JSystem/JAudio2/JASCallback.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"
#include "Game/System/GameThreads.hpp"

bool JASCallbackMgr::regist(JASCallback* pCallback, void* pArgument) {
    GameAudioExclusion criticalSection;
    for (s32 i = 0; i < JASCALLBACK_MAX_CALLBACKS; i++) {
        if (mCallbacks[i].mCallback == nullptr) {
            mCallbacks[i].mCallback = pCallback;
            mCallbacks[i].mArg = pArgument;
            return true;
        }
    }

    return false;
}

s32 JASCallbackMgr::reject(JASCallback* pCallback, void* pArgument) {
    s32 rejectNum = 0;
    GameAudioExclusion criticalSection;
    for (s32 i = 0; i < JASCALLBACK_MAX_CALLBACKS; i++) {
        if (mCallbacks[i].mCallback == pCallback && mCallbacks[i].mArg == pArgument) {
            mCallbacks[i].mCallback = nullptr;
            mCallbacks[i].mArg = nullptr;
            rejectNum++;
        }
    }

    return rejectNum;
}

void JASCallbackMgr::callback() {
    for (s32 i = 0; i < JASCALLBACK_MAX_CALLBACKS; i++) {
        if (mCallbacks[i].mCallback != nullptr) {
            s32 result = mCallbacks[i].mCallback(mCallbacks[i].mArg);
            if (result < 0) {
                mCallbacks[i].mCallback = nullptr;
                mCallbacks[i].mArg = nullptr;
            }
        }
    }
}
