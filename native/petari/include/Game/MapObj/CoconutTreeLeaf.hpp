#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CoconutTreeLeafGroup;

class CoconutTreeLeaf : public LiveActor {
public:
    CoconutTreeLeaf(const CoconutTreeLeafGroup* pGroup, MtxPtr pJointMtx, const Vec3f* pWindDirection);

    virtual void init(const TableIter& rIter);
    virtual MtxPtr getBaseMtx() const;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void update(f32 windAccel, f32 windRandomAccel);

    const CoconutTreeLeafGroup* mGroup;
    Vec3f mTipPosition;
    Vec3f mRestTipPosition;
    Vec3f mRestUp;
    Vec3f mAxisX;
    Vec3f mAxisY;
    Vec3f mAxisZ;
    MtxPtr mJointMtx;
    s32 mWindStep;
    s32 mWindStartStep;
    Vec3f mWindAccel;
    const Vec3f* mWindDirection;
};

class CoconutTreeLeafGroup : public LiveActor {
public:
    CoconutTreeLeafGroup(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    s32 mNumLeaves;
    CoconutTreeLeaf** mLeaves;
    Vec3f mWindDirection;
    f32 mDistanceToPlayer;
};
