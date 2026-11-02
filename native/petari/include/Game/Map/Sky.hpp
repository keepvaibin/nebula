#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MirrorReflectionModel;
class ProjmapEffectMtxSetter;
class SpaceInner;

class Sky : public LiveActor {
public:
    /// @brief Creates a new `Sky`.
    /// @param pName A pointer to the null-terminated name of the object.
    Sky(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void calcAnim();
    virtual void control();
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

    virtual void calcAndSetBaseMtx();
    virtual void initModel(const char*);

    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
};
