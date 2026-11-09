#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MirrorReflectionModel;
class ProjmapEffectMtxSetter;
class SpaceInner;

class Sky : public LiveActor {
public:
    Sky(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void initModel(const char*);

    void appearSpaceInner();
    void disappearSpaceInner();

    void exeWait();
    void exeChange();

    SpaceInner* mSpaceInner;
    MirrorReflectionModel* mReflectionModel;
};

class ProjectionMapSky : public Sky {
public:
    ProjectionMapSky(const char*);

    virtual void calcAndSetBaseMtx() override;
    virtual void initModel(const char*) override;

    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
};
