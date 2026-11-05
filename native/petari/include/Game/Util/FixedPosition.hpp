#pragma once

#include <nebula_geometry.hpp>

class LiveActor;

class FixedPosition {
public:
    FixedPosition(const LiveActor*, const char*, const Vec3f&, const Vec3f&);
    FixedPosition(const LiveActor*, const Vec3f&, const Vec3f&);
    FixedPosition(MtxPtr, const Vec3f&, const Vec3f&);
    FixedPosition(const LiveActor*, const char*, const LiveActor*);

    void init(MtxPtr, const Vec3f&, const Vec3f&);
    void calc();
    void setBaseMtx(MtxPtr);
    void setLocalTrans(const Vec3f&);
    void copyTrans(Vec3f* pTrans) const {
        mMtx.getTrans(*pTrans);
    }

    void copyRotate(Vec3f* pRotate) const {
        mMtx.getEuler(*pRotate);
        *pRotate = *pRotate * _180_PI;
    }

    MtxPtr mBaseMtx;
    Vec3f mLocalTrans;
    Vec3f mLocalRotate;
    Pos3f mMtx;
    bool mNormalizeScale;
};
