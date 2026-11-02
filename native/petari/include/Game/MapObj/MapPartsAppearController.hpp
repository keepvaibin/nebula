#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"

class PostureHolder;

class MapPartsAppearController : public MapPartsFunction {
public:
    MapPartsAppearController(LiveActor*);

    virtual void init(const JMapInfoIter&);
    virtual void start();
    virtual void end();
    virtual bool receiveMsg(u32);

    void storeCurrentPosture();
    void initSwitchMessenger(const JMapInfoIter&);
    void startAppear();
    void startKill();
    void appearHost();
    void killHost();
    void exeWait();
    void exeDisappear();

    s32 mSignMotionType;
    PostureHolder* mPostureHolder;
    u8 _20;
};
