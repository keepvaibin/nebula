#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaBattleMapStair;
class ModelObj;

class KoopaFireStairs : public LiveActor {
public:
    KoopaFireStairs(const char* pName, bool);

    virtual ~KoopaFireStairs();
    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void makeActorDead();
    virtual void calcAndSetBaseMtx();

    void setInfo(const KoopaBattleMapStair*, const Vec3f*);
    void exeFly();
    void exeBreak();

    bool mIsKoopaJr;
    const KoopaBattleMapStair* mStair;
    Vec3f _94;
    Vec3f _A0;
    Vec3f _AC;
    ModelObj* mBreakModel;
};
