#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFpView : public MarioState {
public:
    MarioFpView(MarioActor*);

    virtual bool start();
    virtual bool update();
    virtual bool close();
    virtual f32 getBlurOffset() const;

    void forceClose();

    u16 mDelay;
    bool mIsActive;
    f32 mBlurOffset;
};
