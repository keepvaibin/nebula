#include "JSystem/JKernel/JKRDisposer.hpp"
#include "Game/System/NebulaArena.hpp"

JKRDisposer::JKRDisposer() : mLink(this) {
    mHeap = NebulaArena::findFromRoot(this);

    if (mHeap) {
        mHeap->lock();
        mHeap->mDisposerList.append(&mLink);
        mHeap->unlock();
    }
}

JKRDisposer::~JKRDisposer() {
    if (mHeap) {
        mHeap->lock();
        mHeap->mDisposerList.remove(&mLink);
        mHeap->unlock();
    }
}
