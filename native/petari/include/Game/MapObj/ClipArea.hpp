#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ClipAreaShape;

class ClipArea : public LiveActor {
public:
    ClipArea(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }
    virtual bool isInArea(const Vec3f&, f32) const;

    void initBaseMatrix(const TableIter&);
    void setShape(ClipAreaShape*);

    Pos3f mBaseMtx;
    ClipAreaShape* mShape;
};

namespace MR {
    void setClipSphereArea(ClipArea*);
    void setClipCenterBoxArea(ClipArea*);
    void setClipBottomBoxArea(ClipArea*);
    void setClipTopConeArea(ClipArea*);
    void setClipBottomConeArea(ClipArea*);
};  // namespace MR
