#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class PalmIsland : public LiveActor {
public:
    PalmIsland(const char*);

    virtual ~PalmIsland();
    virtual void init(const JMapInfoIter&);

    void exeFloat();
    void exeWait();

    s32 _8C;
    TVec3f _90;
    s32 _9C;
};
