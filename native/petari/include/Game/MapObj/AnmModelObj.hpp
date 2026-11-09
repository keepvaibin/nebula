#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class ActorCameraInfo;

class AnmModelObj : public MapObjActor {
public:
    AnmModelObj(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override {
    }
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override {
    }
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override {
    }
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override {
    }
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override {
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

    virtual void init(const TableIter&) override;
    virtual bool isOnStartAnmTrigger() const override;
};

class AnmModelSwitchMoveEndKill : public AnmModelSwitchMove {
public:
    AnmModelSwitchMoveEndKill(const char* pName) : AnmModelSwitchMove(pName) {
    }

    virtual bool isKilledAtMoveDone() const override;
};

class AnmModelGroundOnMove : public AnmModelObj {
public:
    AnmModelGroundOnMove(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual bool isOnStartAnmTrigger() const override;
    virtual bool isKilledAtMoveDone() const override {
        return true;
    }
};

class AnmModelBindMove : public AnmModelObj {
public:
    AnmModelBindMove(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual bool isOnStartAnmTrigger() const override;
    virtual bool isKilledAtMoveDone() const override {
        return false;
    }
    virtual bool isRepeat() const override {
        return true;
    }
};

class AnmModelSwitchMoveEventCamera : public AnmModelSwitchMove {
public:
    AnmModelSwitchMoveEventCamera(const char* pName);

    virtual void init(const TableIter&) override;
    virtual bool isDone() const override;
    virtual void startInner() override;
    virtual void stopInner() override;

    ActorCameraInfo* mCameraInfo;
};
