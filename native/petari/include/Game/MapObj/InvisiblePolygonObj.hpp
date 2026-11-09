#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class InvisiblePolygonObj : public LiveActor {
public:
    InvisiblePolygonObj(const char*);

    virtual void init(const TableIter&) override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }

    void initBaseMtx();
    void initCollision(const TableIter&);

    Mtx34f mBaseMtx;
};
