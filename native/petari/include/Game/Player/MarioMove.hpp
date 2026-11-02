#pragma once

#include "Game/Player/MarioModule.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class MarioActor;

class MarioMove : public MarioModule {
public:
    MarioMove(MarioActor*);
    void initAfter();

    TVec3f mHeadVec;
    TVec3f mFrontVec;
    TVec3f mSideVec;

    TVec3f mHeadVec2;
    TVec3f mFrontVec2;
    TVec3f mSideVec2;

    f32 _50;
    f32 _54;
};
