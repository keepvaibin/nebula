#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"

class PostureHolder;

class MapPartsAppearController : public MapPartsFunction {
public:
    MapPartsAppearController(LiveActor*);

    virtual void init(const TableIter&) override;
    virtual void start() override;
    virtual void end() override;
    virtual bool receiveMsg(u32) override;

    void storeCurrentPosture();
    void initSwitchMessenger(const TableIter&);
    void startAppear();
    void startKill();
    void appearHost();
    void killHost();
    void exeWait();
    void exeDisappear();

    s32 mSignMotionType;
    PostureHolder* mPostureHolder;
    u8 mIsAppeared;
};
