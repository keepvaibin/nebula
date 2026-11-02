#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

class LiveActor;

class FixedPosition {
public:
    FixedPosition(const LiveActor*, const char*, const TVec3f&, const TVec3f&);
    FixedPosition(const LiveActor*, const TVec3f&, const TVec3f&);
    FixedPosition(MtxPtr, const TVec3f&, const TVec3f&);
    FixedPosition(const LiveActor*, const char*, const LiveActor*);

    void init(MtxPtr, const TVec3f&, const TVec3f&);
    void calc();
    void setBaseMtx(MtxPtr);
    void setLocalTrans(const TVec3f&);
    void copyTrans(TVec3f* pTrans) const {
        mMtx.getTrans(*pTrans);
    }

    void copyRotate(TVec3f* pRotate) const {
        mMtx.getEuler(*pRotate);
        *pRotate = *pRotate * _180_PI;
    }

    MtxPtr mBaseMtx;
    TVec3f mLocalTrans;
    TVec3f mLocalRotate;
    TPos3f mMtx;
    bool mNormalizeScale;
};
