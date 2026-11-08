#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class KameckBeam;
class KameckBeamEventListener;
class KameckFireBall;
class KameckTurtle;

class KameckBeamHolder : public DeriveActorGroup< KameckBeam > {
public:
    KameckBeamHolder();
};

class KameckFireBallHolder : public DeriveActorGroup< KameckFireBall > {
public:
    KameckFireBallHolder();
};

class KameckBeamTurtleHolder : public DeriveActorGroup< KameckTurtle > {
public:
    KameckBeamTurtleHolder();
};

namespace MR {
    void createKameckBeamHolder();
    void createKameckFireBallHolder();
    void createKameckBeamTurtleHolder();
    KameckBeam* startFollowKameckBeam(s32, MtxPtr, f32, const Vec3f&, KameckBeamEventListener*);
    KameckBeam* getKameckBeam();
    KameckFireBall* getKameckFireBall();
    KameckTurtle* getKameckBeamTurtle();
};  // namespace MR
