#include "JSystem/JKernel/JKRFileLoader.hpp"
#include "Game/System/NebulaKernel.hpp"
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

JKRFileLoader* JKRFileLoader::sCurrentVolume;
JSUList< JKRFileLoader > JKRFileLoader::sVolumeList = JSUList< JKRFileLoader >();
NebulaMutex JKRFileLoader::sVolumeListMutex;

JKRFileLoader::JKRFileLoader() : JKRDisposer(), mLoaderLink(this) {
    mLoaderName = nullptr;
    mLoaderType = 0;
    _34 = 0;
}

JKRFileLoader::~JKRFileLoader() {
    if (sCurrentVolume == this) {
        sCurrentVolume = nullptr;
    }
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
        JSUPtrLink* current = sVolumeList.mHead;

        while (current != nullptr) {
            resource = reinterpret_cast< JKRFileLoader* >(current->mData)->getResource(0, pName);

            if (resource != nullptr) {
                break;
            }

            current = current->mNext;
        }
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
