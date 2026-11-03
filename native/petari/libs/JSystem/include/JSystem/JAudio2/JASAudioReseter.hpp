#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

struct JASAudioReseter {
    JASAudioReseter();
    ~JASAudioReseter();
    bool start(u32, bool);
    void resume();
    s32 checkDone() const;
    s32 calc();
    static s32 callback(void*);

    u32 mTime;
    f32 mDSPLevel;
    s32 mDoneFlag;
    bool mThreadStopFlag;
};
