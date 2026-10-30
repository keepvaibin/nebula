#include "JSystem/JKernel/JKRDvdFile.hpp"
#include "Game/System/NebulaKernel.hpp"

JKRDvdFile::JKRDvdFile() : mDvdLink(this) {
    initiate();
}

JKRDvdFile::JKRDvdFile(s32 entryNum) : mDvdLink(this) {
    initiate();
    mIsAvailable = open(entryNum);
    if (!mIsAvailable) {
        return;
    } else {
        return;
    }
}

JKRDvdFile::~JKRDvdFile() {
    close();
}

void JKRDvdFile::initiate(void) {
    mMutex1.init();
    mMutex2.init();
    field_0x50 = NULL;
    field_0x58 = 0;
}

bool JKRDvdFile::open(const char* name) {
    if (!mIsAvailable) {
        mIsAvailable = NebulaDisc::open(name, &mFile);
        if (mIsAvailable) {
            sDvdList.append(&mDvdLink);
        }
    }
    return mIsAvailable;
}

bool JKRDvdFile::open(s32 entryNum) {
    if (!mIsAvailable) {
        mIsAvailable = NebulaDisc::open(entryNum, &mFile);
        if (mIsAvailable) {
            sDvdList.append(&mDvdLink);
        }
    }
    return mIsAvailable;
}

// A disc file holds nothing on the host: closing it only takes it off the list.
void JKRDvdFile::close() {
    if (mIsAvailable) {
        mIsAvailable = false;
        sDvdList.remove(&mDvdLink);
    }
}

// One reader at a time; it sleeps while the host reads (NebulaDisc::read).
s32 JKRDvdFile::readData(void* pDst, s32 length, s32 offset) {
    mMutex1.lock();
    const s32 result = NebulaDisc::read(&mFile, pDst, length, offset);
    mMutex1.unlock();
    return result;
}

s32 JKRDvdFile::writeData(void const* param_0, s32 length, s32 param_2) {
    return -1;
}

JSUList< JKRDvdFile > JKRDvdFile::sDvdList;
