#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class DemoCtrlBase;

class RailMoveObj : public MapObjActor {
public:
    RailMoveObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void setupInitInfo(const TableIter&, MapObjActorInitInfo*);
    virtual bool tryStartMove();
    virtual void startMoveInner();
    virtual void move();
    virtual void doAtEndPoint();
    virtual bool endMove();
    virtual Vec3f* getSoundCalcPos();

    bool isMoving() const;
    void exeWait();
    void exeMove();

    Vec3f mRailClippingCenter;
    u8 mWasRailMoving;
};

class RailMoveObjPress : public RailMoveObj {
public:
    RailMoveObjPress(const char*);

    virtual void init(const TableIter&) override;
};

class RailMoveObjBreakAtEnd : public RailMoveObj {
public:
    RailMoveObjBreakAtEnd(const char*);

    virtual void doAtEndPoint() override;
};

class RailMoveIndirectObj : public RailMoveObj {
public:
    inline RailMoveIndirectObj(const char* pName) : RailMoveObj(pName) {
    }

    virtual void connectToScene(const MapObjActorInitInfo&) override;
};

class RailMoveObjSwitchShadow : public RailMoveObj {
public:
    inline RailMoveObjSwitchShadow(const char* pName) : RailMoveObj(pName) {
    }

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;

    void startOnShadow();
};

class RailDemoMoveObj : public RailMoveObj {
public:
    RailDemoMoveObj(const char*);

    virtual void init(const TableIter&) override;
    virtual bool tryStartMove() override;
    virtual void startMoveInner() override;
    virtual void move() override;
    virtual bool endMove() override;

    DemoCtrlBase* mDemoControl;
};

class RailRotateMoveObj : public RailMoveObj {
public:
    RailRotateMoveObj(const char*);

    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void setupInitInfo(const TableIter&, MapObjActorInitInfo*) override;

    bool tryStartRotateAtPoint();
    bool tryStartRotateBetweenPoints();
};
