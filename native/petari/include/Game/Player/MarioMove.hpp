#pragma once

#include "Game/Player/MarioModule.hpp"
#include <nebula_geometry.hpp>

class MarioActor;

class MarioMove : public MarioModule {
public:
    MarioMove(MarioActor*);
    void initAfter();

    Vec3f mHeadVec;
    Vec3f mFrontVec;
    Vec3f mSideVec;

    Vec3f mHeadVec2;
    Vec3f mFrontVec2;
    Vec3f mSideVec2;

};
