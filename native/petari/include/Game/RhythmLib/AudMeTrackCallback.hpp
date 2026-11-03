#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

typedef s32 (*AudCallbackFunc)(s32, s32, void*);

struct AudCallbackData {
    AudCallbackData() : mFunc(), mData() {
    }

    AudCallbackFunc mFunc;
    void* mData;
};

class AudCallback {
public:
    AudCallback() {};

    bool regist(AudCallbackFunc, void*);
    void callback(s32, s32);

    AudCallbackData mCallbackData[32];
};

class AudMeTrackCallback : public AudCallback {
public:
    static AudCallback sMeTrackCallback;

    AudMeTrackCallback() {
    }

    static bool regist(AudCallbackFunc, void*);
    static void callback(s32, s32);
};
