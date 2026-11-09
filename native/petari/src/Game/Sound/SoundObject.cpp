#include "Game/Sound/SoundObject.hpp"
#include "Game/Sound/EffectManager.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/Sound/SoundStarter.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include "Game/System/NebulaArena.hpp"

SoundObject::SoundObject(Vec3f* pPos, u8 numHandles, NebulaArena* pArena)
    : SoundHandles(new(pArena, 0) SoundHandle[numHandles], numHandles), mPos(pPos) {
    mIsAllocated = true;
}

SoundObject::~SoundObject() {
    dispose();
    delete[] mHandles;
}

void SoundObject::dispose() {
    for (u32 idx = 0; idx < mNumHandles; idx++) {
        if (mHandles[idx].isSoundAttached()) {
            mHandles[idx].getSound()->stop();
        }
    }
    mIsAllocated = false;
}

void SoundObject::process() {
    if (!mIsAllocated) {
        return;
    }
    setPos(*mPos);
}

SoundHandle* SoundObject::startSound(SoundID soundID) {
    if (!mIsAllocated) {
        return nullptr;
    }

    SoundStarter* starter = SoundGlobal< SoundStarter >::getInstance();

    SoundHandle* handle = getHandleSoundID(soundID);
    if (handle == nullptr) {
        handle = getFreeHandleNotReserved();
    }

    if (handle == nullptr) {
        handle = getLowPrioSound(soundID);
    }

    if (handle != nullptr) {
        starter->startSound(soundID, handle, mPos);
    }

    return handle;
}

SoundHandle* SoundObject::startSoundIndex(SoundID soundID, u8 index) {
    if (!mIsAllocated) {
        return nullptr;
    }

    SoundHandle* handle = &mHandles[index];
    SoundGlobal< SoundStarter >::getInstance()->startSound(soundID, handle, mPos);
    return handle;
}

SoundHandle* SoundObject::startLevelSound(SoundID soundID) {
    if (!mIsAllocated) {
        return nullptr;
    }

    EffectManager* seMgr = SoundGlobal< EffectManager >::getInstance();

    SoundHandle* handle = getHandleSoundID(soundID);
    if (handle != nullptr) {
        handle->getSound()->updateLifeTime(1);
    } else {
        handle = getFreeHandleNotReserved();

        if (handle == nullptr) {
            handle = getLowPrioSound(soundID);
        }

        if (handle != nullptr) {
            seMgr->startSound(soundID, handle, mPos);

            if (handle->getSound() != nullptr) {
                handle->getSound()->setLifeTime(1, false);
            }
        }
    }

    return handle;
}

SoundHandle* SoundObject::startLevelSoundIndex(SoundID soundID, u8 index) {
    if (!mIsAllocated) {
        return nullptr;
    }
    EffectManager* seMgr = SoundGlobal< EffectManager >::getInstance();
    SoundHandle* handle = getHandleSoundID(soundID);
    if (handle != nullptr) {
        handle->getSound()->updateLifeTime(1);
        return handle;
    }

    handle = &mHandles[index];
    seMgr->startSound(soundID, handle, mPos);
    if (handle->getSound() != nullptr) {
        handle->getSound()->setLifeTime(1, false);
    }

    return handle;
}

void SoundObject::stopSound(SoundID soundID, u32 fadeTime) {
    SoundHandle* handle = getHandleSoundID(soundID);
    if (handle != nullptr) {
        handle->getSound()->stop(fadeTime);
    }
}

SoundHandle* SoundObject::getLowPrioSound(SoundID soundID) {
    SoundHandle* handle;
    const StdSoundInfo* info = StdSoundInfo::getInstance();
    u32 lowPrio = 0xFFFF;
    SoundHandle* prioHandle = nullptr;

    for (int idx = 0; idx < mNumHandles; idx++) {
        handle = &mHandles[idx];
        u32 prio = info->getPriority(handle->getSound()->getID());
        if (prio < lowPrio) {
            lowPrio = prio;
            prioHandle = handle;
        }
    }

    if (info->getPriority(soundID) >= lowPrio) {
        return prioHandle;
    }
    return nullptr;
}

SoundHandle* SoundObject::getFreeHandleNotReserved() {
    for (int idx = 0; idx < mNumHandles; idx++) {
        if (mHandles[idx].getSound() == nullptr) {
            return &mHandles[idx];
        }
    }
    return nullptr;
}
