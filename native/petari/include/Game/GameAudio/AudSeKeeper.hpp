#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JKernel/JKRDisposer.hpp>

class AudSoundObject;
class J3DJoint;

class AudSeKeeperBox {
public:
    AudSeKeeperBox();

    ~AudSeKeeperBox();
    void update();
    void add(J3DJoint*, u16);
    void updateJointPos();

    AudSoundObject* mSoundObj;
    J3DJoint* mJoint;
    TVec3f mPos;
    u16 mHash;
};

class AudSeKeeper : public JKRDisposer {
public:
    AudSeKeeper(LiveActor*, int);

    ~AudSeKeeper();
    void update();
    void makeBox(const char*, const char*);
    bool playLevel(const char*, const char*);
    int getBoxEqualName(const char*);

    LiveActor* mActor;
    AudSeKeeperBox* mBoxes;
    int mMaxBoxes;
    int mNumBoxes;
};
