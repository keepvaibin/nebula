#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MarioNullBck : public LiveActor {
public:
    MarioNullBck(const char*);

    virtual ~MarioNullBck();

    virtual void init(const TableIter&);
    virtual void appear();

    void getLastPos(Vec3f*);
    bool getFramePos(f32, Vec3f*, Vec3f*);

    u16 mLastFrame;  // the frame last set (getFramePos)
};
