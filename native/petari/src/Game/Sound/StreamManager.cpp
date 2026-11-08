#include "Game/Sound/StreamManager.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/SoundInfo.hpp"
#include "Game/Sound/StreamSource.hpp"

StreamManager::StreamManager(bool setInstance) : SoundGlobal< StreamManager >(setInstance) {
    streamDataMgr_ = nullptr;
    soundStrategyMgr = nullptr;
    mAudience = nullptr;
    mParams.init();
    mActivity.init();
}

bool StreamManager::startSound(SoundID id, SoundHandle* handle, const Vec3f* posPtr) {
    if (handle != nullptr && handle->isSoundAttached()) {
        (*handle)->stop();
    }

    s32 streamFileEntry = streamDataMgr_->getStreamFileEntry(id);
    if (streamFileEntry < 0) {
        return false;
    }

    StreamSound* stream = newStream_();
    SoundInfo* soundInfo = SoundGlobal< SoundInfo >::getInstance();

    int category = -1;
    if (soundInfo != nullptr) {
        category = soundInfo->getCategory(id);
    }

    if (stream == nullptr) {
        return false;
    }

    stream->startID_(id, streamFileEntry, posPtr, mAudience, category);
    if (soundInfo != nullptr) {
        soundInfo->getStreamInfo(id, stream);
    }

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
    StreamSound* stream = new StreamSound(this, soundStrategyMgr);
    if (stream == nullptr) {
        return nullptr;
    }

    mStreamList.append(stream);
    return stream;
}
