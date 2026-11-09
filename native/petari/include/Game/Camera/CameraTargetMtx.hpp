#pragma once

#include "Game/Camera/CameraTargetObj.hpp"
#include <nebula_geometry.hpp>

class CameraTargetMtx : public CameraTargetObj {
public:
    CameraTargetMtx(const char*);

    virtual void movement() override;

    virtual const Vec3f& getPosition() const override;
    virtual const Vec3f& getUpVec() const override;
    virtual const Vec3f& getFrontVec() const override;
    virtual const Vec3f& getSideVec() const override;
    virtual const Vec3f& getLastMove() const override;
    virtual const Vec3f& getGroundPos() const override;
    virtual const Vec3f& getGravityVector() const override;

    virtual CubeCameraArea* getCubeCameraArea() const override;

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
