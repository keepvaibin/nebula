#include "JSystem/JKernel/JKRDisposer.hpp"
#include "Game/System/NebulaArena.hpp"

JKRDisposer::JKRDisposer() {
    mHeap = NebulaArena::findFromRoot(this);

    if (mHeap) {
        mHeap->mDisposerList.append(this);
    }
}

JKRDisposer::~JKRDisposer() {
    if (mHeap) {
        mHeap->mDisposerList.remove(this);
    }
}
