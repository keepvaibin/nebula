#include "Game/Sound/SequenceManager.hpp"
#include "Game/Sound/SequenceSound.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/SoundMemory.hpp"
#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/System/NebulaArena.hpp"

SequenceManager::SequenceManager(bool setInstance) : SoundGlobal< SequenceManager >(setInstance), mAudience() {
    seqDataMgr = nullptr;
    mNumTracks = 16;
    mMove.init();
    mSoundActivity.init();
}

void SequenceManager::freeDeadSeq_() {
    NebulaListLink< SequenceSound >* i = mSeqList.getFirst();
    while (i != nullptr) {
        SequenceSound* seq = i->getObject();
        NebulaListLink< SequenceSound >* next = i->getNext();
        if (seq->isDead()) {
            mSeqList.remove(i);
            delete seq;
        }
        i = next;
    }
}

bool SequenceManager::startSound(SoundID id, SoundHandle* handle, const Vec3f* posPtr) {
    if (handle != nullptr && handle->isSoundAttached()) {
        (*handle)->stop();
    }

    SeqData seqData;
    if (!seqDataMgr->getSeqData(id, &seqData)) {
        return false;
    }

    SequenceSound* seq = beginStartSeq_();
    if (seq != nullptr) {
        seq->startID_(id, posPtr, mAudience, id.getGroupID(), mNumTracks, seqData);
        if (endStartSeq_(seq, handle)) {
            StdSoundInfo::getInstance()->applySound(id, seq);
            return true;
        }
    }

    return false;
}

void SequenceManager::calc() {
    mMove.calc();

    NebulaListLink< SequenceSound >* i;
    for (i = mSeqList.getFirst(); i != mSeqList.getEnd(); i = i->getNext()) {
        i->getObject()->managerCalc_();
    }
    freeDeadSeq_();
}

void SequenceManager::stop() {
    NebulaListLink< SequenceSound >* i;
    for (i = mSeqList.getFirst(); i != mSeqList.getEnd(); i = i->getNext()) {
        i->getObject()->stop();
    }
}

void SequenceManager::stop(u32 fadeTime) {
    NebulaListLink< SequenceSound >* i;
    for (i = mSeqList.getFirst(); i != mSeqList.getEnd(); i = i->getNext()) {
        i->getObject()->stop(fadeTime);
    }
}

void SequenceManager::stopSoundID(SoundID id) {
    if (!id.isAnonymous()) {
        NebulaListLink< SequenceSound >* i;
        for (i = mSeqList.getFirst(); i != mSeqList.getEnd(); i = i->getNext()) {
            if ((u32)i->getObject()->getID() == (u32)id) {
                i->getObject()->stop();
            }
        }
    }
}

void SequenceManager::mixOut() {
    NebulaListLink< SequenceSound >* i;
    for (i = mSeqList.getFirst(); i != mSeqList.getEnd(); i = i->getNext()) {
        i->getObject()->managerMixOut_(mMove.mParams, mSoundActivity);
    }
}

SequenceSound* SequenceManager::beginStartSeq_() {
    SequenceSound* seq = new (SoundMemory::arena(), 0) SequenceSound(this);
    return seq;
}

bool SequenceManager::endStartSeq_(SequenceSound* seq, SoundHandle* handle) {
    SequenceSound* sound = seq->getObject();
    if (sound != nullptr) {
        if (sound->isAlive()) {
            mSeqList.append(seq);
            if (handle != nullptr) {
                sound->attachHandle(handle);
            }
            return true;
        }
        delete sound;
    }
    return false;
}
