#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class ActorCameraInfo;

class AnmModelObj : public MapObjActor {
public:
    AnmModelObj(const char* pName);

    virtual void init(const TableIter&);
    virtual void control() {
    }
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) {
    }
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) {
    }
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) {
    }
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) {
    }
    virtual bool isOnStartAnmTrigger() const = 0;
    virtual bool isDone() const;
    virtual bool isKilledAtMoveDone() const {
        return false;
    }
    virtual bool isRepeat() const {
        return false;
    }
    virtual void startInner() {
    }
    virtual void moveInner() {
    }
    virtual void stopInner() {
    }

    void exeWait();
    void exeMove();
    void exeDone();

    Vec3f mJointPos;
    // The level sound looped once done (only the Heavenly Beach rock has one), or null.
    const char* mDoneLevelSe;
};

class AnmModelSwitchMove : public AnmModelObj {
public:
    AnmModelSwitchMove(const char* pName);

    virtual void init(const TableIter&);
    virtual bool isOnStartAnmTrigger() const;
};

class AnmModelSwitchMoveEndKill : public AnmModelSwitchMove {
public:
    AnmModelSwitchMoveEndKill(const char* pName) : AnmModelSwitchMove(pName) {
    }

    virtual bool isKilledAtMoveDone() const;
};

class AnmModelGroundOnMove : public AnmModelObj {
public:
    AnmModelGroundOnMove(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool isOnStartAnmTrigger() const;
    virtual bool isKilledAtMoveDone() const {
        return true;
    }
};

class AnmModelBindMove : public AnmModelObj {
public:
    AnmModelBindMove(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual bool isOnStartAnmTrigger() const;
    virtual bool isKilledAtMoveDone() const {
        return false;
    }
    virtual bool isRepeat() const {
        return true;
    }
};

class AnmModelSwitchMoveEventCamera : public AnmModelSwitchMove {
public:
    AnmModelSwitchMoveEventCamera(const char* pName);

    virtual void init(const TableIter&);
    virtual bool isDone() const;
    virtual void startInner();
    virtual void stopInner();

    ActorCameraInfo* mCameraInfo;
};
