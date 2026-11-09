#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Scene/MovementControlType.hpp"

class DemoCastGroupHolder;
class DemoExecutor;
class DemoSimpleCastHolder;
class DemoStartRequestHolder;
class LayoutActor;
class LiveActor;
class ResourceHolder;

class DemoDirector : public NameObj {
public:
    DemoDirector(const char* pName);

    virtual void movement() override;

    void startDemoProgrammable(NameObj*, const char*, bool, MR::MovementControlType);
    void startDemoTimeKeep(NameObj*, const char*, MR::MovementControlType, bool, const char*);
    void startDemoExecutor(NameObj*, const char*, MR::MovementControlType, const char*);
    const char* getCurrentDemoName() const;
    void endDemo(NameObj* pRequester, const char* pDemoName, bool waitCameraInterpolating);
    bool isExistTimeKeepDemo(const char* pDemoName) const;
    bool registerDemoCast(LiveActor*, const TableIter&);
    bool registerDemoCast(LiveActor*, const char* pDemoName, const TableIter&);
    void registerDemoSimpleCast(LiveActor*);
    void registerDemoSimpleCast(LayoutActor*);
    void registerDemoSimpleCast(NameObj*);
    bool tryStartDemoRequested();
    void startDemo(NameObj*, const char*, bool, MR::MovementControlType);
    void startDemoRequested();
    void doDemoEndRequest();

    bool mIsActive;
    DemoExecutor* mExecutor;
    bool mIsWaitingCamera;
    DemoCastGroupHolder* mCastGroupHolder;
    DemoCastGroupHolder* mCastSubGroupHolder;
    DemoSimpleCastHolder* mSimpleCastHolder;
    ResourceHolder* mResourceHolder;
    DemoStartRequestHolder* mStartRequestHolder;
    NameObj* mDemoStarter;
    const char* mDemoName;
    MR::MovementControlType mMovementType;
    bool mUseCinemaFrame;
};
