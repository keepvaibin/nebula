#include "Game/LiveActor/ActorStateKeeper.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/NebulaArena.hpp"

ActorStateKeeper::ActorStateKeeper() : mLength(0), mStates(), mCurrentState(nullptr) {
}

void ActorStateKeeper::addState(ActorStateBaseInterface* pInterface, const Nerve* pNerve) {
    State& rState = mStates.room(mLength + 1)[mLength];
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
