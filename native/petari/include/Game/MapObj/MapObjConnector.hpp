#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class MapObjConnector {
public:
    MapObjConnector(LiveActor*);

    bool attachToUnder();
    bool attachToBack();
    bool attach(const TVec3f&);
    void connect();
    void connect(LiveActor*);

    LiveActor* mHost;
    CollisionParts* mParts;
    TMtx34f _8;
};
