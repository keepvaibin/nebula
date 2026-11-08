#include "Game/LiveActor/ActorStateKeeper.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    const s32 sFirstCapacity = 8;
};  // namespace

ActorStateKeeper::ActorStateKeeper() : mCapacity(0), mLength(0), mStates(nullptr), mCurrentState(nullptr) {
}

void ActorStateKeeper::addState(ActorStateBaseInterface* pInterface, const Nerve* pNerve) {
    if (mLength == mCapacity) {
        const s32 capacity = mCapacity != 0 ? mCapacity * 2 : ::sFirstCapacity;
        State* const pStates = new (NebulaArena::findFromRoot(this), 0) State[capacity];
        for (s32 i = 0; i < mLength; i++) {
            pStates[i] = mStates[i];
        }
        delete[] mStates;
        mStates = pStates;
        mCapacity = capacity;
    }

    State& rState = mStates[mLength];
    rState.mInterface = pInterface;
    rState.mNerve = pNerve;
    mLength++;
}

bool ActorStateKeeper::updateCurrentState() {
    return mCurrentState != nullptr && mCurrentState->mInterface->update();
}

void ActorStateKeeper::startState(const Nerve* pNerve) {
    mCurrentState = findStateInfo(pNerve);

    if (mCurrentState != nullptr) {
        mCurrentState->mInterface->appear();
    }
}

void ActorStateKeeper::endState(const Nerve* pNerve) {
    mCurrentState = findStateInfo(pNerve);

    if (mCurrentState != nullptr && !mCurrentState->mInterface->mIsDead) {
        mCurrentState->mInterface->kill();
    }
}

ActorStateKeeper::State* ActorStateKeeper::findStateInfo(const Nerve* pNerve) {
    for (s32 i = 0; i < mLength; i++) {
        if (mStates[i].mNerve == pNerve) {
            return &mStates[i];
        }
    }

    return nullptr;
}
