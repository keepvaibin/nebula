#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapObjConnector {
public:
    MapObjConnector(LiveActor*);

    bool attachToUnder();
    bool attachToBack();
    bool attach(const Vec3f&);
    void connect();
    void connect(LiveActor*);

    LiveActor* mHost;
    CollisionParts* mParts;
    Mtx34f mOffsetMtx;
};
