#include "JSystem/JAudio2/JASDriverIF.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "JSystem/JAudio2/JASVoice.hpp"

void JASDriver::setOutputLevel(f32 param_0) {
    JASVoice::setOutputLevel(param_0);
}

// A full channel's gain on its way to the mix.
f32 JASDriver::getChannelGain() {
    return 0.18310547f;
}

f32 JASDriver::getOutputLevel() {
    return JASVoice::getOutputLevel();
}

u32 JASDriver::JAS_SYSTEM_OUTPUT_MODE = 0x00000001;

void JASDriver::setOutputMode(u32 param_0) {
    JAS_SYSTEM_OUTPUT_MODE = param_0;
}

u32 JASDriver::getOutputMode() {
    return JASDriver::JAS_SYSTEM_OUTPUT_MODE;
}

GameEvent JASDriver::sSubFrameDone;

void JASDriver::waitSubFrame() {
    sSubFrameDone.wait();
}

JASCallbackMgr JASDriver::sSyncCallback;

JASCallbackMgr JASDriver::sSubFrameCallback;

JASCallbackMgr JASDriver::sUpdateDacCallback;

int JASDriver::rejectCallback(DriverCallback callback, void* param_1) {
    int r31 = sSyncCallback.reject(callback, param_1);
    r31 += sSubFrameCallback.reject(callback, param_1);
    r31 += sUpdateDacCallback.reject(callback, param_1);
    return r31;
}

bool JASDriver::registerSyncCallback(DriverCallback callback, void* param_1) {
    return sSyncCallback.regist(callback, param_1);
}

bool JASDriver::registerSubFrameCallback(DriverCallback callback, void* param_1) {
    return sSubFrameCallback.regist(callback, param_1);
}

void JASDriver::subframeCallback() {
    sSubFrameCallback.callback();
}

void JASDriver::syncCallback() {
    sSyncCallback.callback();
}

