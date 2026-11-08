#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorJointCtrl;
class LodCtrl;

class ModelObj : public LiveActor {
public:
    // A category below its None value (-2) takes the default: MapObj draw buffer, MapObjDecoration movement and calc-anim.
    ModelObj(const char* pName, const char* pModelName, MtxPtr pMtx, int drawBufferType, int movementType, int calcAnimType,
             bool deferMaterials);

    virtual void init(const TableIter&) override;
    virtual void calcAndSetBaseMtx() override;

    MtxPtr mMtx;
};

class ModelObjNpc : public LiveActor {
public:
    ModelObjNpc(const char*, const char*, MtxPtr);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    MtxPtr mMtx;
    LodCtrl* mLodCtrl;
    ActorJointCtrl* mJointCtrl;
};
