#pragma once

#include "Game/Camera/CameraTargetObj.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CameraTargetMtx : public CameraTargetObj {
public:
    CameraTargetMtx(const char*);
    virtual ~CameraTargetMtx();

    virtual void movement();

    virtual const TVec3f& getPosition() const;
    virtual const TVec3f& getUpVec() const;
    virtual const TVec3f& getFrontVec() const;
    virtual const TVec3f& getSideVec() const;
    virtual const TVec3f& getLastMove() const;
    virtual const TVec3f& getGroundPos() const;
    virtual const TVec3f& getGravityVector() const;

    virtual CubeCameraArea* getCubeCameraArea() const;

    void invalidateLastMove();

    void setMtx(MtxPtr mtx) {
        mMatrix.set(mtx);
    }

    TPos3f mMatrix;
    TVec3f mPosition;
    TVec3f mLastMove;
    TVec3f mGravityVector;
    TVec3f mUp;
    TVec3f mFront;
    TVec3f mSide;
    bool mInvalidLastMove;
    CubeCameraArea* mCameraArea;
};
