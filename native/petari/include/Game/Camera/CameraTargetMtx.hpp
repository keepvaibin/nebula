#pragma once

#include "Game/Camera/CameraTargetObj.hpp"
#include <nebula_geometry.hpp>

class CameraTargetMtx : public CameraTargetObj {
public:
    CameraTargetMtx(const char*);

    virtual void movement();

    virtual const Vec3f& getPosition() const;
    virtual const Vec3f& getUpVec() const;
    virtual const Vec3f& getFrontVec() const;
    virtual const Vec3f& getSideVec() const;
    virtual const Vec3f& getLastMove() const;
    virtual const Vec3f& getGroundPos() const;
    virtual const Vec3f& getGravityVector() const;

    virtual CubeCameraArea* getCubeCameraArea() const;

    void invalidateLastMove();

    void setMtx(MtxPtr mtx) {
        mMatrix.set(mtx);
    }

    Pos3f mMatrix;
    Vec3f mPosition;
    Vec3f mLastMove;
    Vec3f mGravityVector;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mSide;
    bool mInvalidLastMove;
    CubeCameraArea* mCameraArea;
};
