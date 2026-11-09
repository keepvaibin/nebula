#pragma once

#include "Game/NPC/NPCActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class TalkMessageCtrl;

class Rabbit : public NPCActor {
public:
    enum SeVersion {
        SeVersion_NORMAL = 0,
        SeVersion_WATER = 1,
    };

    enum Behavior {
        Behavior_Wait = 0,
        Behavior_Unknown = 1,
        Behavior_Demo1 = 2,
        Behavior_Demo2 = 3,
        Behavior_UpJump = 4,
        Behavior_LongJump = 5,
    };

    Rabbit(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void calcRailPos(Vec3f*);
    bool isNeedTurn(const Vec3f&);
    void updateJump();

    void exeAppear();
    void exeAppearLand();
    void exeWait();
    void exeGoal();
    void exeFinish();
    void exeForwardLand();
    void exePreJump();
    void exeMove();
    void exeBackwardLand();
    void exePreJumpBack();
    void exeNear();
    void exeReaction();
    void exeTalk();
    void exeJumpV();
    void exeJumpH();

    s32 mBehavior;
    bool mIsFalling;
    u16 mIsLanded;
    s32 mWaitStep;
    f32 mJumpHeight;
    f32 mJumpSpeed;
    f32 mMoveSpeed;
    Vec3f mMoveDir;
    f32 mJumpScale;
};
