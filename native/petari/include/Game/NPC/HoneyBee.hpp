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

    /// @brief Creates a new `HoneyBee`.
    /// @param pName A pointer to the null-terminated name of the object.
    HoneyBee(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    void exeWait();
    void exeReaction();
    void exeFly();
    void exeDropLecture();
    void exeJumpLecture();
    void exeFlyLectureA();
    void exeFlyLectureB();

    s32 _15C;
    TMtx34f _160;
    TVec3f _190;
    s32 mArg0;
    f32 _1A0;
};
