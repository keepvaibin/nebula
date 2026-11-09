#include "Game/Sound/StreamManager.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/SoundMemory.hpp"
#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/System/NebulaArena.hpp"

StreamManager::StreamManager(bool setInstance) : SoundGlobal< StreamManager >(setInstance) {
    mAudience = nullptr;
    mParams.init();
    mActivity.init();
}

bool StreamManager::startSound(SoundID id, SoundHandle* handle, const Vec3f* posPtr) {
    if (handle != nullptr && handle->isSoundAttached()) {
        (*handle)->stop();
    }

    const StdSoundInfo* info = StdSoundInfo::getInstance();
    s32 streamFileEntry = info->getStreamFileEntry(id);
    if (streamFileEntry < 0) {
        return false;
    }

    StreamSound* stream = newStream_();
    if (stream == nullptr) {
        return false;
    }

    stream->startID_(id, streamFileEntry, posPtr, mAudience, id.getGroupID());
    info->applyStream(id, stream);

    if (handle != nullptr) {
        stream->attachHandle(handle);
    }

    return false;
}

void StreamManager::freeDeadStream_() {
    NebulaListLink< StreamSound >* i = mStreamList.getFirst();
    while (i != nullptr) {
        StreamSound* stream = i->getObject();
        NebulaListLink< StreamSound >* next = i->getNext();
        if (stream->isDead()) {
            mStreamList.remove(i);
            delete stream;
        }
        i = next;
    }
}

void StreamManager::calc() {
    NebulaListLink< StreamSound >* i;
    mParams.calc();
    for (i = mStreamList.getFirst(); i != nullptr; i = i->getNext()) {
        i->getObject()->managerCalc_();
    }
    freeDeadStream_();
}

void StreamManager::stop() {
    NebulaListLink< StreamSound >* i;
    for (i = mStreamList.getFirst(); i != nullptr; i = i->getNext()) {
        i->getObject()->stop();
    }
}

void StreamManager::stop(u32 fadeTime) {
    NebulaListLink< StreamSound >* i;
    for (i = mStreamList.getFirst(); i != nullptr; i = i->getNext()) {
        i->getObject()->stop(fadeTime);
    }
}

void StreamManager::stopSoundID(SoundID id) {
    NebulaListLink< StreamSound >* i;
    for (i = mStreamList.getFirst(); i != nullptr; i = i->getNext()) {
        if ((u32)i->getObject()->getID() == (u32)id) {
            i->getObject()->stop();
        }
    }
}

void StreamManager::mixOut() {
    NebulaListLink< StreamSound >* i;
    for (i = mStreamList.getFirst(); i != nullptr; i = i->getNext()) {
        i->getObject()->managerMixOut_(mParams.mParams, mActivity);
    }
}

StreamSound* StreamManager::newStream_() {
    StreamSound* stream = new (SoundMemory::arena(), 0) StreamSound(this);
    if (stream == nullptr) {
        return nullptr;
    }

    mStreamList.append(stream);
    return stream;
}
