#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JKernel/JKRDisposer.hpp>

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
