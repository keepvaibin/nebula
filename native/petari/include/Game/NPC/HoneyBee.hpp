#pragma once

#include "Game/NPC/NPCActor.hpp"

class HoneyBee : public NPCActor {
public:
    enum InitAction {
        InitAction_Wait,
        InitAction_JumpLecture,
        InitAction_Fly,
        InitAction_Sleep,
        InitAction_Stick,
        InitAction_Gatekeeper,
        InitAction_DropLecture,
        InitAction_Flustered,
        InitAction_FlyLectureA,
        InitAction_FlyLectureB,
    };

    HoneyBee(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;


    void exeWait();
    void exeReaction();
    void exeFly();
    void exeDropLecture();
    void exeJumpLecture();
    void exeFlyLectureA();
    void exeFlyLectureB();

    s32 mRailPointNo;
    Mtx34f mTalkMtx;
    Vec3f mClippingCenter;
    s32 mInitAction;
    f32 mFloatOffset;
};
