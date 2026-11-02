#include "JSystem/JKernel/JKRDisposer.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"

JKRDisposer::JKRDisposer() : mLink(this) {
    mHeap = JKRHeap::findFromRoot(this);

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
