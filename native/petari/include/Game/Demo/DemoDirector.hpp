#pragma once

#include "Game/NameObj/NameObj.hpp"

class DemoCastGroupHolder;
class DemoExecutor;
class DemoSimpleCastHolder;
class DemoStartRequestHolder;
class LayoutActor;
class LiveActor;
class ResourceHolder;

class DemoDirector : public NameObj {
public:
    /// @brief Creates a new `DemoDirector`.
    /// @param pName A pointer to the null-terminated name of the object.
    DemoDirector(const char* pName);

    virtual void movement();

    void startDemoProgrammable(NameObj*, const char*, bool, s32);
    void startDemoTimeKeep(NameObj*, const char*, s32, bool, const char*);
    void startDemoExecutor(NameObj*, const char*, s32, const char*);
    const char* getCurrentDemoName() const;
    void endDemo(NameObj*, const char*, bool);
    bool isExistTimeKeepDemo(const char*) const;
    bool registerDemoCast(LiveActor*, const TableIter&);
    bool registerDemoCast(LiveActor*, const char*, const TableIter&);
    void registerDemoSimpleCast(LiveActor*);
    void registerDemoSimpleCast(LayoutActor*);
    void registerDemoSimpleCast(NameObj*);
    bool tryStartDemoRequested();
    void startDemo(NameObj*, const char*, bool, s32);
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
    s32 mMovementType;
    bool mUseCinemaFrame;
};
