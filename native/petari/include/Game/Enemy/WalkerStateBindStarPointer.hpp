#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;

class WalkerStateBindStarPointer : public ActorStateBase< LiveActor > {
public:
    WalkerStateBindStarPointer(LiveActor* pHost, AnimScaleController* pController);

    virtual void appear();
    virtual void kill();

    bool tryStartPointBind() const;
    void exeBind();

    AnimScaleController* mScaleController;  ///< The controller for scaling the actor during animations.
    s32 mUpdateCounter;                     ///< Counter that is incremented in exeBind. Once it exceeds 5, it updates the actor.
    bool mHasEffect;                        ///< Does the host actor contain the "Touch" effect name?
};
