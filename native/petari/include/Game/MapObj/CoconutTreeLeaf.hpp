#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CoconutTreeLeafGroup;

class CoconutTreeLeaf : public LiveActor {
public:
    CoconutTreeLeaf(const CoconutTreeLeafGroup* pGroup, MtxPtr pJointMtx, const TVec3f* pWindDirection);

    virtual ~CoconutTreeLeaf();
    virtual void init(const JMapInfoIter& rIter);
    virtual MtxPtr getBaseMtx() const;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void update(f32 windAccel, f32 windRandomAccel);

    const CoconutTreeLeafGroup* mGroup;
    TVec3f mTipPosition;
    TVec3f mRestTipPosition;
    TVec3f mRestUp;
    TVec3f mAxisX;
    TVec3f mAxisY;
    TVec3f mAxisZ;
    MtxPtr mJointMtx;
    s32 mWindStep;
    s32 mWindStartStep;
    TVec3f mWindAccel;
    const TVec3f* mWindDirection;
};

class CoconutTreeLeafGroup : public LiveActor {
public:
    CoconutTreeLeafGroup(const char* pName);

    virtual ~CoconutTreeLeafGroup();
    virtual void init(const JMapInfoIter& rIter);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    s32 mNumLeaves;
    CoconutTreeLeaf** mLeaves;
    TVec3f mWindDirection;
    f32 mDistanceToPlayer;
};
