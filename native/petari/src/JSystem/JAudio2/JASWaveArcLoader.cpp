#include "JSystem/JAudio2/JASWaveArcLoader.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASLoadThread.hpp"
#include "JSystem/JAudio2/JASMutex.hpp"
#include "JSystem/JAudio2/JASTaskThread.hpp"

#include <cstring>
#include <stdint.h>
#include "Game/System/GameAssets.hpp"
#include <nebula_format.h>

char JASWaveArcLoader::sCurrentDir[DIR_MAX] = "/AudioRes/Waves/";

void JASWaveArcLoader::setCurrentDir(char const* dir) {
    nebula_snprintf(sCurrentDir, DIR_MAX - 1, "%s", dir);
    u32 len = strlen(sCurrentDir);
    if (sCurrentDir[len - 1] != '/') {
        sCurrentDir[len] = '/';
        sCurrentDir[len + 1] = '\0';
    }
}

char* JASWaveArcLoader::getCurrentDir() {
    return sCurrentDir;
}

GameEvent JASWaveArc::sLoadDone;

void JASWaveArc::waitLoadDone() {
    sLoadDone.wait();
}

JASWaveArc::JASWaveArc() {
    _48 = 0;
    mStatus = 0;
    _58 = 0;
    mData = nullptr;
    mEntryNum = -1;
    mFileLength = 0;
    mMutex.init();
}

JASWaveArc::~JASWaveArc() {
}

bool JASWaveArc::loadSetup(u32 param_0) {
    JASMutexLock mutexLock(&mMutex);
    if (_58 != param_0) {
        return false;
    }
    if (mStatus != 1) {
        return false;
    }
    _48 = 1;
    mStatus = 2;
    return true;
}

bool JASWaveArc::eraseSetup() {
    JASMutexLock mutexLock(&mMutex);
    if (mStatus == 0) {
        return false;
    }
    if (mStatus == 1) {
        mStatus = 0;
        return false;
    }
    _48 = 0;
    mStatus = 0;
    return true;
}

void JASWaveArc::loadCallback(void* this_) {
    loadCallbackParams* tmp = (loadCallbackParams*)this_;
    JASWaveArc* wavArc = tmp->mWavArc;
    const u8* const pData = static_cast< const u8* >(GameAssets::map(tmp->mEntryNum));
    if (pData == nullptr) {
        return;
    }
    {
        JASMutexLock mutexLock(&wavArc->mMutex);
        wavArc->mData = pData;
    }
    if (wavArc->loadSetup(tmp->_C)) {
        wavArc->onLoadDone();
        sLoadDone.signal();
    }
}

bool JASWaveArc::sendLoadCmd() {
    JASMutexLock mutexLock(&mMutex);
    _48 = 0;
    mStatus = 1;
    loadCallbackParams commandInfo;
    commandInfo.mWavArc = this;
    commandInfo.mEntryNum = mEntryNum;
    commandInfo._C = ++_58;

    if (JASLoad::getThreadPointer()->sendCmdMsg(loadCallback, &commandInfo, sizeof(commandInfo)) == 0) {
        eraseSetup();
        return false;
    }
    return true;
}


bool JASWaveArc::erase() {
    JASMutexLock mutexLock(&mMutex);
    if (mStatus == 0) {
        return false;
    }

    if (eraseSetup()) {
        onEraseDone();
    }
    return true;
}

void JASWaveArc::setEntryNum(s32 entryNum) {
    GameAssets::File file;
    if (entryNum < 0 || !GameAssets::open(entryNum, &file)) {
        return;
    }
    mFileLength = file.mLength;
    mEntryNum = entryNum;
}

