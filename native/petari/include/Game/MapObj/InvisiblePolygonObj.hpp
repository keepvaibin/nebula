#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class InvisiblePolygonObj : public LiveActor {
public:
    InvisiblePolygonObj(const char*);

    virtual ~InvisiblePolygonObj();
    virtual void init(const JMapInfoIter&);
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mBaseMtx;
    }

    void initBaseMtx();
    void initCollision(const JMapInfoIter&);

    Mtx34f mBaseMtx;
};
