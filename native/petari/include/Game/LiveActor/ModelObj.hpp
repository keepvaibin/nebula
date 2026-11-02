#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorJointCtrl;
class LodCtrl;

class ModelObj : public LiveActor {
public:
    ModelObj(const char*, const char*, MtxPtr, int, int, int, bool);

    virtual void init(const JMapInfoIter&) override;
    virtual void calcAndSetBaseMtx() override;

    MtxPtr mMtx;
};

class ModelObjNpc : public LiveActor {
public:
    ModelObjNpc(const char*, const char*, MtxPtr);

    virtual ~ModelObjNpc();
    virtual void init(const JMapInfoIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    MtxPtr mMtx;
    LodCtrl* mLodCtrl;
    ActorJointCtrl* mJointCtrl;
};
