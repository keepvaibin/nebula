#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

template < typename T >
class JointControlDelegator;
struct JointControllerInfo;
class MsgSharedGroup;

class FlipPanel : public MapObjActor {
public:
    FlipPanel(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void endClipped();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeFrontLand();
    void exeBackLand();
    void exeWait();
    void exeEndPrepare();
    void exeEnd();
    bool calcJointMove(Pos3f*, const JointControllerInfo&);
    bool checkPlayerOnTop();

    JointControlDelegator< FlipPanel >* mDelegator;
    bool mIsPlayerOn;
    u8 mIsFlipped;
    s32 mJumpCheckCount;
    bool mIsReverse;
};

class FlipPanelObserver : public LiveActor {
public:
    FlipPanelObserver(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeComplete();
    void exeDemoWait();

    MsgSharedGroup* mGroup;
    s32 mFlippedNum;
    s32 mDemoDelay;
    s32 mPowerStarId;
    u8 mIsHeavensDoor;
};
