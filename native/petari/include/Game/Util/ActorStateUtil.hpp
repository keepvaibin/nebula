#pragma once

class ActorStateBaseInterface;
class LiveActor;
class Nerve;
class NerveExecutor;

// The update functions appear the state on the nerve's first step, then update it. They return true on the step the
// state finishes (its update returned true), after killing it if it is still alive; the NextNerve forms then set
// pNextNerve.
namespace MR {
    // Before any initActorState.
    void initActorStateKeeper(NerveExecutor* pExecutor, s32 capacity);
    // Inits the state and makes it the one run while pNerve is set.
    void initActorState(NerveExecutor* pExecutor, ActorStateBaseInterface* pActorState, const Nerve* pNerve, const char* pStateName);

    bool updateActorState(LiveActor* pActor, ActorStateBaseInterface* pActorState);
    bool updateActorStateAndNextNerve(LiveActor* pActor, ActorStateBaseInterface* pActorState, const Nerve* pNextNerve);

    // The state of the executor's current nerve (false without one). The keeper appears and kills it as the nerve
    // changes; this only updates it.
    bool updateActorState(NerveExecutor* pExecutor);
    bool updateActorState(NerveExecutor* pExecutor, ActorStateBaseInterface* pActorState);
    bool updateActorStateAndNextNerve(NerveExecutor* pExecutor, const Nerve* pNextNerve);
    bool updateActorStateAndNextNerve(NerveExecutor* pExecutor, ActorStateBaseInterface* pActorState, const Nerve* pNextNerve);
};  // namespace MR
