#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaBattleMapStair;
class ModelObj;

class KoopaFireStairs : public LiveActor {
public:
    KoopaFireStairs(const char* pName, bool);

    virtual ~KoopaFireStairs();
    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void makeActorDead();
    virtual void calcAndSetBaseMtx();

    void setInfo(const KoopaBattleMapStair*, const TVec3f*);
    void exeFly();
    void exeBreak();

    bool mIsKoopaJr;
    const KoopaBattleMapStair* mStair;
    TVec3f _94;
    TVec3f _A0;
    TVec3f _AC;
    ModelObj* mBreakModel;
};
