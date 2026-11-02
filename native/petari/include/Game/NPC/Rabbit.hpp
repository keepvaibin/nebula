#pragma once

#include "Game/NPC/NPCActor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

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

    virtual ~Rabbit();
    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void calcRailPos(TVec3f*);
    bool isNeedTurn(const TVec3f&);
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
    bool _160;
    u16 _162;
    u16 _164;
    f32 _168;
    f32 _16C;
    f32 _170;
    TVec3f _174;
    f32 _180;
};
