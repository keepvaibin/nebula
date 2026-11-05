#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>


class ActorCameraInfo;
class ModelObj;

class BlackHole : public LiveActor {
public:
    BlackHole(const char*);

    virtual ~BlackHole();
    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);

    bool tryStartDemoCamera();
    void initMapToolInfo(const JMapInfoIter&);
    void initModel();
    void initCubeBox();
    bool isInCubeBox(const Vec3f&) const;
    void updateModelScale(f32, f32);
    void exeWait();
    void exeDemo();
    void exeDisappear();

    ModelObj* mBlackHoleModel;
    Vec3f _90;
    f32 _9C;
    f32 _A0;
    Box3f* _A4;
    Pos3f _A8;
    Pos3f _D8;
    ActorCameraInfo* mCameraInfo;
};
