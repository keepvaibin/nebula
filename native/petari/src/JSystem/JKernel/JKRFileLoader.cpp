#include "JSystem/JKernel/JKRFileLoader.hpp"
#include "Game/System/GameThreads.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

JKRFileLoader* JKRFileLoader::sCurrentVolume;
JSUList< JKRFileLoader > JKRFileLoader::sVolumeList = JSUList< JKRFileLoader >();
GameMutex JKRFileLoader::sVolumeListMutex;

JKRFileLoader::JKRFileLoader() : JKRDisposer(), mLoaderLink(this) {
    mLoaderName = nullptr;
    mLoaderType = 0;
    _34 = 0;
}

JKRFileLoader::~JKRFileLoader() {
    sVolumeListMutex.lock();
    if (sCurrentVolume == this) {
        sCurrentVolume = nullptr;
    }
    sVolumeListMutex.unlock();
}

void JKRFileLoader::unmount() {
    if (_34 == 0) {
        return;
    }

    if (--_34 != 0) {
        return;
    }

    delete this;
}

void* JKRFileLoader::getGlbResource(const char* pName, JKRFileLoader* pLoader) {
    void* resource = nullptr;

    if (pLoader != nullptr) {
        resource = pLoader->getResource(0, pName);
    } else {
        sVolumeListMutex.lock();
        JSUPtrLink* current = sVolumeList.mHead;

        while (current != nullptr) {
            resource = reinterpret_cast< JKRFileLoader* >(current->mData)->getResource(0, pName);

            if (resource != nullptr) {
                break;
            }

            current = current->mNext;
        }
        sVolumeListMutex.unlock();
    }

    return resource;
}

void JKRFileLoader::initializeVolumeList() {
    sVolumeListMutex.init();
}

void JKRFileLoader::prependVolumeList(JSULink< JKRFileLoader >* pLoader) {
    sVolumeListMutex.lock();

    sVolumeList.prepend(pLoader);

    sVolumeListMutex.unlock();
}

void JKRFileLoader::removeVolumeList(JSULink< JKRFileLoader >* pLoader) {
    sVolumeListMutex.lock();

    sVolumeList.remove(pLoader);

    sVolumeListMutex.unlock();
}
