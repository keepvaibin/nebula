#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaBattleMapStair;
class ModelObj;

class KoopaFireStairs : public LiveActor {
public:
    KoopaFireStairs(const char* pName, bool isKoopaJr);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void makeActorDead() override;
    virtual void calcAndSetBaseMtx() override;

    void setInfo(const KoopaBattleMapStair*, const Vec3f*);
    void exeFly();
    void exeBreak();

    bool mIsKoopaJr;
    const KoopaBattleMapStair* mStair;
    Vec3f mFront;
    Vec3f mStartPos;
    Vec3f mFlyUpDir;
    ModelObj* mBreakModel;
};
