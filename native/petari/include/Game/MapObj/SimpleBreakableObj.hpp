#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class ActorCameraInfo;
class BenefitItemObj;

class SimpleBreakableObjBase : public MapObjActor {
public:
    SimpleBreakableObjBase(const char* pName) : MapObjActor(pName) {
    }

    virtual ~SimpleBreakableObjBase() override {
    }
    // No control of its own: the game defines none (MapObjActor's), and only SimpleBreakableObj, which has one, is made.
};

class SimpleBreakableObj : public SimpleBreakableObjBase {
public:
    SimpleBreakableObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override {
    }

    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override {
    }

    void setStateBreak();
    void startBreak();
    void endBreak();
    bool isStateBreaking() const;
    void appearItem();
    void exeWait() {
    }

    void exeTryStartDemo();
    void exeDemo();

    ActorCameraInfo* mCameraInfo;
    s32 mBreakOffsetFrame;
    s32 mSwitchDeadDelay;
    u16 mBreakFlags;
    s32 mItemType;
    s32 mItemNum;
    BenefitItemObj* mKinokoOneUp;
    bool mPlayRiddleSound;
    bool mBreakEnded;
};

class SimpleBreakableIndirectObj : public SimpleBreakableObj {
public:
    SimpleBreakableIndirectObj(const char* pName) : SimpleBreakableObj(pName) {
    }

    virtual ~SimpleBreakableIndirectObj() override {
    }
};
