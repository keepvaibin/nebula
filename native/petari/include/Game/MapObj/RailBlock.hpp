#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class RailBlock : public LiveActor {
public:
    RailBlock(const char* pName);

    virtual void init(const TableIter& rIter) override;

    void startMove();
    void stopMove();
    void exeWait();
    void exeMove();
    void exeTerminate();

    s32 mRailSpeed;
    s32 mMoveTime;
    bool mIsConnectedToRail;
};
