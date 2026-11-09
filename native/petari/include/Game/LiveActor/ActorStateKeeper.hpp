#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/Array.hpp"

/// @brief The states a nerve executor runs per nerve. The list grows in the arena the keeper lies in.
class ActorStateKeeper {
public:
    ActorStateKeeper();

    void addState(ActorStateBaseInterface*, const Nerve*);
    bool updateCurrentState();
    void startState(const Nerve*);
    void endState(const Nerve*);

private:
    struct State {
        ActorStateBaseInterface* mInterface;
        const Nerve* mNerve;
    };

    State* findStateInfo(const Nerve*);

    s32 mLength;
    MR::GrowBuffer< State > mStates;
    State* mCurrentState;
};
