#include "JSystem/JAudio2/JASAudioReseter.hpp"
#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASVoiceSlot.hpp"
#include "JSystem/JAudio2/JASDriverIF.hpp"

JASAudioReseter::JASAudioReseter() {
    mTime = 0;
    mDoneFlag = true;
    mThreadStopFlag = false;
    mDSPLevel = JASDriver::getOutputLevel();
}

JASAudioReseter::~JASAudioReseter() {
}

bool JASAudioReseter::start(u32 time, bool threadStopFlag) {
    if (mDoneFlag == false) {
        return false;
    }
    mThreadStopFlag = threadStopFlag;
    GameAudioExclusion critical;
    if (!JASDriver::registerSyncCallback(callback, this)) {
        return false;
    } else {
        mDSPLevel = JASDriver::getOutputLevel();
        mTime = time;
        mDoneFlag = false;
        return true;
    }
}

void JASAudioReseter::resume() {
    JASDriver::setOutputLevel(mDSPLevel);
}

s32 JASAudioReseter::checkDone() const {
    return mDoneFlag;
}

s32 JASAudioReseter::calc() {
    if (mTime == 0) {
        for (size_t i = 0; i < 64; i++) {
            JASVoiceSlot* handle = JASVoiceSlot::getHandle(i);
            if (handle->getStatus() == 0) {
                handle->drop();
            }
        }
        if (mThreadStopFlag != false) {
            JASDriver::stopClock();
        }
        mDoneFlag = 1;
        return -1;
    }

    mTime--;
    JASDriver::setOutputLevel(mTime * JASDriver::getOutputLevel() / (mTime + 1));
    return 0;
}

s32 JASAudioReseter::callback(void* reseter) {
    JASAudioReseter* i_this = (JASAudioReseter*)reseter;
    return i_this->calc();
}
