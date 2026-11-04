#include "JSystem/JAudio2/JASBank.hpp"
#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "JSystem/JAudio2/JASBasicInst.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"


static JASOscillator::Point const OSC_RELEASE_TABLE[] = {
    {0x0001, 0x000A, 0x0000},
    {0x000F, 0x0000, 0x0000},
};

JASChannel* JASBank::noteOnOsc(int progNo, u8 pitch, u8 velocity, u16 priority, void (*param_4)(u32, JASChannel*, JASVoice*, void*),
                               void* param_5) {
    static const JASOscillator::Data OSC_ENV = {0, 1.0f, nullptr, OSC_RELEASE_TABLE, 1.0f, 0.0f};

    JASChannel* channel = new JASChannel(param_4, param_5);
    if (channel == nullptr) {
        return nullptr;
    }
    channel->setPriority(priority);
    channel->mProgNo = progNo;
    channel->mChannelType = JASChannel::CH_OSC;
    channel->setSourceRate(523.0005f);
    channel->setKey(pitch - channel->mWaveInfo.mBaseKey);
    channel->setVelocity(velocity);
    channel->setOscInit(0, &OSC_ENV);
    if (!channel->play()) {
        return nullptr;
    }
    return channel;
}
