#include "JSystem/JAudio2/JASWaveArcLoader.hpp"
#include "Game/System/NebulaKernel.hpp"
#include "JSystem/JAudio2/JASDvdThread.hpp"
#include "JSystem/JAudio2/JASMutex.hpp"
#include "JSystem/JAudio2/JASTaskThread.hpp"
#include "JSystem/JKernel/JKRDvdRipper.hpp"
#include <cstring>
#include <stdint.h>
#include "Game/System/NebulaDisc.hpp"

JASHeap* JASWaveArcLoader::sAramHeap;

JASHeap* JASWaveArcLoader::getRootHeap() {
    if (JASWaveArcLoader::sAramHeap) {
        return JASWaveArcLoader::sAramHeap;
    }
    return JASKernel::getAramHeap();
}

char JASWaveArcLoader::sCurrentDir[DIR_MAX] = "/AudioRes/Waves/";

void JASWaveArcLoader::setCurrentDir(char const* dir) {
    strcpy(sCurrentDir, dir);
    u32 len = strlen(sCurrentDir);
    if (sCurrentDir[len - 1] != '/') {
        sCurrentDir[len] = '/';
        sCurrentDir[len + 1] = '\0';
    }
}

char* JASWaveArcLoader::getCurrentDir() {
    return sCurrentDir;
}

JASWaveArc::JASWaveArc() : mHeap(this) {
    _48 = 0;
    mStatus = 0;
    _58 = 0;
    mEntryNum = -1;
    mFileLength = 0;
    _5A = 0;
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

void JASWaveArc::loadToAramCallback(void* this_) {
    loadToAramCallbackParams* tmp = (loadToAramCallbackParams*)this_;
    JASWaveArc* wavArc = tmp->mWavArc;
    if (JKRDvdRipper::loadToMainRAM(tmp->mEntryNum, tmp->mBase, EXPAND_SWITCH_UNKNOWN0, wavArc->mFileLength, nullptr,
                                    JKRDvdRipper::ALLOC_DIRECTION_FORWARD, 0, nullptr, nullptr) == nullptr) {
        return;
    }
    wavArc->_5A--;
    if (wavArc->loadSetup(tmp->_C)) {
        wavArc->onLoadDone();
    }
}

bool JASWaveArc::sendLoadCmd() {
    JASMutexLock mutexLock(&mMutex);
    _48 = 0;
    mStatus = 1;
    loadToAramCallbackParams commandInfo;
    commandInfo.mWavArc = this;
    commandInfo.mEntryNum = mEntryNum;
    commandInfo.mBase = static_cast< u8* >(mHeap.getBase());
    commandInfo._C = ++_58;

    _5A++;

    if (JASDvd::getThreadPointer()->sendCmdMsg(loadToAramCallback, &commandInfo, sizeof(commandInfo)) == 0) {
        mHeap.free();
        return false;
    }
    return true;
}

#ifndef TARGET_PC
// Replaced by Game's Overwrite file; only that copy is in the game.
bool JASWaveArc::load(JASHeap* heap) {
    if (mEntryNum < 0) {
        return false;
    }
    JASMutexLock mutexLock(&mMutex);
    if (mStatus != 0) {
        return false;
    }
    if (heap == nullptr) {
        heap = JASWaveArcLoader::getRootHeap();
    }
    if (mHeap.alloc(heap, mFileLength) == false) {
        return false;
    }
    return sendLoadCmd();
}
#endif

bool JASWaveArc::loadTail(JASHeap* heap) {
    if (mEntryNum < 0) {
        return false;
    }
    JASMutexLock mutexLock(&mMutex);
    if (mStatus != 0) {
        return false;
    }
    if (heap == nullptr) {
        heap = JASWaveArcLoader::getRootHeap();
    }
    if (mHeap.allocTail(heap, mFileLength) == false) {
        return false;
    }
    return sendLoadCmd();
}

bool JASWaveArc::erase() {
    return mHeap.free();
}

void JASWaveArc::onDispose() {
    if (eraseSetup()) {
        onEraseDone();
    }
}

void JASWaveArc::setEntryNum(s32 entryNum) {
    NebulaDisc::File file;
    if (entryNum < 0 || !NebulaDisc::open(entryNum, &file)) {
        return;
    }
    mFileLength = file.mLength;
    mEntryNum = entryNum;
}

