#include "JSystem/JKernel/JKRDisposer.hpp"
#include "Game/System/NebulaArena.hpp"

JKRDisposer::JKRDisposer() : mLink(this) {
    mHeap = NebulaArena::findFromRoot(this);

    if (mHeap) {
        mHeap->mDisposerList.append(&mLink);
    }
}

JKRDisposer::~JKRDisposer() {
    if (mHeap) {
        mHeap->mDisposerList.remove(&mLink);
    }
}
