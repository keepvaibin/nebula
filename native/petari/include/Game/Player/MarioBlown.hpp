#pragma once

#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"

class MarioBlown : public MarioState {
public:
    MarioBlown(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    f32 vecKillActor240(const TVec3f& rVec) {
        return MR::vecKillElement(rVec, mActor->_240, &_18);
    }

    u16 mTimer;
    u16 _14;
    TVec3f _18;
    bool _24;
    bool _25;
};
