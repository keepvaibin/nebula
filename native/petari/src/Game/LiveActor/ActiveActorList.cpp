#include "Game/LiveActor/ActiveActorList.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"

ActiveActorList::ActiveActorList(s32 maxCount) : mActorList(new LiveActor*[maxCount]), mCurCount(0), mMaxCount(maxCount) {
    clear();
}

void ActiveActorList::addActor(LiveActor* pActor) {
    if (!isFull()) {
        mActorList[mCurCount] = pActor;
        mCurCount++;
    }
}

void ActiveActorList::removeDeadActor() {
    s32 i = 0;
    while (i < mCurCount) {
        if (MR::isDead(mActorList[i])) {
            mCurCount--;
            mActorList[i] = mActorList[mCurCount];
            mActorList[mCurCount] = nullptr;
        } else {
            i++;
        }
    }
}

void ActiveActorList::clear() {
    for (s32 i = 0; i < mMaxCount; i++) {
        mActorList[i] = nullptr;
    }

    mCurCount = 0;
}

void ActiveActorList::killAll() {
    for (s32 i = 0; i < mCurCount; i++) {
        if (!MR::isDead(mActorList[i])) {
            mActorList[i]->kill();
        }

        mActorList[i] = nullptr;
    }

    mCurCount = 0;
}
