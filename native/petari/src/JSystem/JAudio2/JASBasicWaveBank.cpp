#include "JSystem/JAudio2/JASBasicWaveBank.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include "JSystem/JAudio2/JASMutex.hpp"
#include "Game/System/NebulaArena.hpp"
#include <stdint.h>

JASBasicWaveBank::JASBasicWaveBank() {
    mWaveTable = nullptr;
    mHandleCount = 0;
    mWaveGroupArray = nullptr;
    mGroupCount = 0;
    mMutex.init();
}

JASBasicWaveBank::~JASBasicWaveBank() {
    delete[] mWaveTable;
    for (int i = 0; i < mGroupCount; i++) {
        delete mWaveGroupArray[i];
    }

    delete[] mWaveGroupArray;
}

JASBasicWaveBank::TWaveGroup* JASBasicWaveBank::getWaveGroup(u32 param_0) {
    if (param_0 >= mGroupCount) {
        return nullptr;
    }

    return mWaveGroupArray[param_0];
}

void JASBasicWaveBank::setGroupCount(u32 param_0, NebulaArena* heap) {
    for (int i = 0; i < mGroupCount; i++) {
        delete mWaveGroupArray[i];
    }

    delete[] mWaveGroupArray;
    mGroupCount = (u16)param_0;
    mWaveGroupArray = new (heap, 0) TWaveGroup*[param_0];
    for (int i = 0; i < mGroupCount; i++) {
        mWaveGroupArray[i] = new (heap, 0) TWaveGroup(this);
    }
}

void JASBasicWaveBank::setWaveTableSize(u32 count, NebulaArena* heap) {
    delete[] mWaveTable;
    mWaveTable = new (heap, 0) TWaveInfo*[count];
    JASCalc::bzero(mWaveTable, sizeof(TWaveHandle*) * count);
    mHandleCount = (u16)count;
}

void JASBasicWaveBank::incWaveTable(JASBasicWaveBank::TWaveGroup const* pWaveGroup) {
    JASMutexLock lock(&mMutex);
    TWaveInfo** table;
    TWaveInfo* info;
    u32 id;
    for (u32 i = 0; i < pWaveGroup->getWaveCount(); i++) {
        table = mWaveTable;
        info = &pWaveGroup->mCtrlWaveArray[i];

        id = info->getWaveID();
        TWaveInfo*& entry = table[id];
        info->mPrev = nullptr;
        info->mNext = entry;
        if (entry != nullptr) {
            entry->mPrev = info;
        }

        __atomic_store_n(&entry, info, __ATOMIC_RELEASE);
    }
}

void JASBasicWaveBank::decWaveTable(JASBasicWaveBank::TWaveGroup const* waveGroup) {
    JASMutexLock lock(&mMutex);
    for (u32 i = 0; i < waveGroup->getWaveCount(); i++) {
        TWaveInfo* info = &waveGroup->mCtrlWaveArray[i];
        u32 idx = info->mWaveId;

        for (TWaveInfo* info2 = mWaveTable[idx]; info2 != nullptr; info2 = info2->mNext) {
            if (info2 == info) {
                if (info2->mPrev == nullptr) {
                    mWaveTable[idx] = info2->mNext;
                } else {
                    info2->mPrev->mNext = info2->mNext;
                }

                if (info2->mNext != nullptr) {
                    info2->mNext->mPrev = info2->mPrev;
                }

                break;
            }
        }
    }
}

JASWaveHandle* JASBasicWaveBank::getWaveHandle(u32 waveId) const {
    if (waveId >= mHandleCount) {
        return nullptr;
    }

    // Published by the load thread with the release in incWaveTable.
    TWaveInfo* const info = __atomic_load_n(&mWaveTable[waveId], __ATOMIC_ACQUIRE);
    return info != nullptr ? &info->mWaveHandle : nullptr;
}

JASBasicWaveBank::TWaveGroup::TWaveGroup(JASBasicWaveBank* pBank) {
    mBank = pBank;
    mCtrlWaveArray = nullptr;
    mWaveCount = 0;
}

JASBasicWaveBank::TWaveGroup::~TWaveGroup() {
    delete[] mCtrlWaveArray;
}

void JASBasicWaveBank::TWaveGroup::setWaveCount(u32 count, NebulaArena* heap) {
    delete[] mCtrlWaveArray;
    mWaveCount = (u16)count;
    mCtrlWaveArray = new (heap, 0) TWaveInfo[count];

    for (int i = 0; i < count; i++) {
        mCtrlWaveArray[i].mWaveHandle.mArc = this;
        mCtrlWaveArray[i].mWaveHandle.mWaveInfo._24 = &_48;
    }
}

void JASBasicWaveBank::TWaveGroup::setWaveInfo(int index, u32 waveId, const JASWaveInfo& waveInfo) {
    mCtrlWaveArray[index].mWaveId = (u16)waveId;
    mCtrlWaveArray[index].mWaveHandle.mWaveInfo.copy(waveInfo);
    mCtrlWaveArray[index].mWaveHandle.mWaveInfo._24 = &_48;
}

void JASBasicWaveBank::TWaveGroup::onLoadDone() {
    mBank->incWaveTable(this);
}

void JASBasicWaveBank::TWaveGroup::onEraseDone() {
    mBank->decWaveTable(this);
}

const u8* JASBasicWaveBank::TWaveHandle::getWaveLimit() const {
    return mArc->getData() + mArc->getFileSize();
}

const u8* JASBasicWaveBank::TWaveHandle::getWavePtr() const {
    const u8* base = mArc->getData();
    if (base == nullptr) {
        return nullptr;
    }

    return base + mWaveInfo.mAWStartOffs;
}
