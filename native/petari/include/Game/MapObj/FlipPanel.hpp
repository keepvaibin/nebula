#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

template < typename T >
class JointControlDelegator;
struct JointControllerInfo;
class MsgSharedGroup;

class FlipPanel : public MapObjActor {
public:
    FlipPanel(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void endClipped();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeFrontLand();
    void exeBackLand();
    void exeWait();
    void exeEndPrepare();
    void exeEnd();
    bool calcJointMove(TPos3f*, const JointControllerInfo&);
    bool checkPlayerOnTop();

    JointControlDelegator< FlipPanel >* mDelegator;
    MsgSharedGroup* mFlipPanelGroup;
    bool _CC;
    u8 _CD;
    s32 _D0;
    bool mIsReverse;
};

class FlipPanelObserver : public LiveActor {
public:
    FlipPanelObserver(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeComplete();
    void exeDemoWait();

    MsgSharedGroup* _8C;
    s32 _90;
    s32 mDemoDelay;
    s32 mPowerStarId;
    u8 _9C;
};
