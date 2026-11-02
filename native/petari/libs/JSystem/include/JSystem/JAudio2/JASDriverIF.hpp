#pragma once

#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASCallback.hpp"

typedef s32 (*DriverCallback)(void*);

namespace JASDriver {
    void setDSPLevel(f32);
    u16 getChannelLevel_dsp();
    f32 getChannelLevel();
    f32 getDSPLevel();
    void setOutputMode(u32);
    u32 getOutputMode();
    void waitSubFrame();
    extern GameEvent sSubFrameDone;
    int rejectCallback(DriverCallback, void*);
    bool registerSyncCallback(DriverCallback, void*);
    bool registerSubFrameCallback(DriverCallback, void*);
    void subframeCallback();
    void syncCallback();
    void updateDacCallback();

    extern JASCallbackMgr sSyncCallback;
    extern JASCallbackMgr sSubFrameCallback;
    extern JASCallbackMgr sUpdateDacCallback;
    extern u16 MAX_MIXERLEVEL;
    extern u32 JAS_SYSTEM_OUTPUT_MODE;
};  // namespace JASDriver

inline void JAISetOutputMode(u32 mode) {
    JASDriver::setOutputMode(mode);
}
