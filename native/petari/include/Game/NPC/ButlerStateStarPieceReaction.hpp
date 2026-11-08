#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class TalkMessageCtrl;

class ButlerStateStarPieceReaction : public ActorStateBase< LiveActor > {
public:
    ButlerStateStarPieceReaction(LiveActor*, const TableIter&, const char*);

    virtual void init();
    virtual void appear();

    void exeWait();

    TalkMessageCtrl* mTalkMessage;
    bool mIsTalkForce;
};
