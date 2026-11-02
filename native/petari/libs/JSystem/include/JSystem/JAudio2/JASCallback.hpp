#pragma once


typedef s32 JASCallback(void*);

#define JASCALLBACK_MAX_CALLBACKS 32

struct JASCallbackMgr {
    struct TCallback {
        TCallback() : mCallback(nullptr), mArg(nullptr) {
        }

        JASCallback* mCallback;
        void* mArg;
    };

    bool regist(JASCallback*, void*);
    s32 reject(JASCallback*, void*);
    void callback();

    TCallback mCallbacks[JASCALLBACK_MAX_CALLBACKS];
};
