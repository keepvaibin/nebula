#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class PalmIsland : public LiveActor {
public:
    PalmIsland(const char*);

    virtual ~PalmIsland();
    virtual void init(const JMapInfoIter&);

    void exeFloat();
    void exeWait();

    s32 _8C;
    Vec3f _90;
    s32 _9C;
};
