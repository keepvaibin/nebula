#include "Game/AudioLib/AudUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include <JSystem/JAudio2/JASAiCtrl.hpp>
#include <JSystem/JAudio2/JASBankTable.hpp>
#include <JSystem/JAudio2/JASBasicBank.hpp>
#include <JSystem/JAudio2/JASBasicInst.hpp>
#include <JSystem/JAudio2/JASChannel.hpp>
#include <JSystem/JAudio2/JASMutex.hpp>
#include <JSystem/JAudio2/JASTrack.hpp>
#include <JSystem/JAudio2/JASWaveArcLoader.hpp>
#include <JSystem/JSupport/JSupport.hpp>
#include <cstring>
#include "Game/System/GameAssets.hpp"
#include <nebula_format.h>

JASChannel* JASBank::noteOn(JASBank const* pBank, int progNo, u8 pitch, u8 velocity, u16 priority, JASChannel::Callback pCallback,
                            void* pChannelMgr) {
    if (progNo >= 0xf0) {
        return noteOnOsc(progNo - 0xf0, pitch, velocity, priority, pCallback, pChannelMgr);
    }

    if (pBank == nullptr) {
        return nullptr;
    }

    JASInstParam params;
    if (!pBank->getInstParam(progNo, pitch, velocity, &params)) {
        return nullptr;
    }

    JASWaveBank* waveBank = pBank->getWaveBank();
    if (!waveBank) {
        return nullptr;
    }

    JASWaveHandle* waveHandle = waveBank->getWaveHandle(params._18);
    if (!waveHandle) {
        return nullptr;
    }

    const JASWaveInfo* waveInfo = waveHandle->getWaveInfo();
    if (!waveInfo) {
        return nullptr;
    }

    const u8* wavePtr = waveHandle->getWavePtr();
    if (!wavePtr) {
        return nullptr;
    }

    JASChannel* channel = new JASChannel(pCallback, pChannelMgr);
    if (!channel) {
        return nullptr;
    }

    channel->setPriority(priority);
    channel->mWaveInfo = *waveInfo;
    channel->mWavePtr = wavePtr;
    channel->mWaveLimit = waveHandle->getWaveLimit();
    channel->mChannelType = params.mChannelType;
    channel->setBankDisposeID(pBank);
    channel->setInitPitch(params.mPitch);
    channel->setSourceRate(waveInfo->mSampleRate);
    if (params._24 == 0) {
        channel->setKey(pitch - waveInfo->mBaseKey);
    }
    channel->setInitVolume(params.mVolume);
    channel->setVelocity(velocity);
    channel->setInitPan(params.mPan);
    channel->setInitFxmix(params.mFxMix);
    channel->setInitDolby(params.mDolby);
    for (u32 i = 0; i < params.mNumOscillators; i++) {
        channel->setOscInit(i, params.mOscillatorData[i]);
    }
    channel->setDirectRelease(params.mDirectRelease);
    if (!channel->play()) {
        return nullptr;
    }
    return channel;
}

JASChannel* JASTrack::channelStart(JASTrack::TChannelMgr* pChannelMgr, u32 pitch, u32 velocity, u32 updateTimer) {
    JASBank* bank = nullptr;
    if (mBankTable != nullptr) {
        bank = mBankTable->getBank(mBankNumber);
    }

    JASChannel* channel = JASBank::noteOn(bank, mProgNumber, pitch, velocity, mNoteOnPrio | mReleasePrio << 8, channelUpdateCallback, pChannelMgr);
    if (channel == nullptr) {
        if (AudUtil::isProperCurrentStageOrAudioDebugMode()) {
            JASInstParam params;
            if (!bank->getInstParam(mProgNumber, JSULoByte(pitch), JSULoByte(velocity), &params)) {
                return nullptr;
            }

            JASWaveBank* waveBank = bank->getWaveBank();
            if (!waveBank) {
                return nullptr;
            }

            JASWaveHandle* waveHandle = waveBank->getWaveHandle(params._18);
            if (!waveHandle) {
                return nullptr;
            }

            const JASWaveInfo* waveInfo = waveHandle->getWaveInfo();
            if (!waveInfo) {
                return nullptr;
            }

            const u8* wavePtr = waveHandle->getWavePtr();
            if (!wavePtr) {
                return nullptr;
            }
        }
        return nullptr;
    }

    channel->setUpdateTimer(updateTimer);
    channel->setSkipSamples(mSkipSample);
    channel->setVibrateDelay(mVibDelay);
    channel->setTremoloDelay(mTremDelay);
    channel->setParams(pChannelMgr->mParams);

    for (u32 i = 0; i < 6; i++) {
        channel->setMixConfig(i, mMixConfig[i]);
    }

    overwriteOsc(channel);
    if (mDirectRelease != 0) {
        channel->setDirectRelease(mDirectRelease);
    }

    return channel;
}

void JASWaveArc::setFileName(const char* pName) {
    char* currentDir = JASWaveArcLoader::getCurrentDir();
    size_t length = strlen(currentDir);
    length = length + strlen(pName);
    char* path = new (JASKernel::getSystemHeap(), -4) char[length + 1];
    nebula_snprintf(path, length + 1, "%s%s", currentDir, pName);

    const char* fileName = path;

    int entryNum = GameAssets::find(fileName);
    delete[] path;
    if (entryNum < 0) {
        return;
    }
    setEntryNum(entryNum);
}

bool JASWaveArc::load() {
    if (mEntryNum < 0) {
        return false;
    }

    JASMutexLock mutexLock(&mMutex);
    if (mStatus != 0) {
        return false;
    }

    return sendLoadCmd();
}

JASInstParam::JASInstParam()
    : mChannelType(JASChannel::CH_WAVE), _18(0), mOscillatorData(nullptr), mNumOscillators(0), _24(false), mDirectRelease(0) {
}
