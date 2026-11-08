#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"
#include "Game/System/NebulaArena.hpp"

class AudSoundObject;
class Joint;

class AudSeKeeperBox {
public:
    AudSeKeeperBox();

    ~AudSeKeeperBox();
    void update();
    void add(Joint*, u16);
    void updateJointPos();

    AudSoundObject* mSoundObj;
    Joint* mJoint;
    Vec3f mPos;
    u16 mHash;
};

// Level sounds at an actor's joints, one box per joint. Each box is allocated alone: its sound object holds a
// pointer to the box's position.
class AudSeKeeper : public ArenaOwned {
public:
    AudSeKeeper(LiveActor* pActor, int boxNumHint);

    ~AudSeKeeper();
    void update();
    void makeBox(const char* pJointName, const char* pJointName2);
    bool playLevel(const char* pJointName, const char* pSoundName);
    int getBoxEqualName(const char* pJointName);

    LiveActor* mActor;
    MR::Vector< MR::AssignableArray< AudSeKeeperBox* > > mBoxes;
};
