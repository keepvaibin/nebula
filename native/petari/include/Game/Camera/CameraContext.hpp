#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class CameraContext : public NameObj {
public:
    CameraContext();

    void initParams();
    const Pos3f& getViewMtx() const;
    const Pos3f& getInvViewMtx() const;
    f32 getAspect() const;
    f32 getRenderFovy() const;
    void setViewMtx(const Pos3f& rMtx);
    void setNearZ(f32);
    void setFovy(f32);
    void setShakeOffset(f32, f32);
    void updateProjectionMtx();

    f32 getNearZ() const {
        return mNearZ;
    }

    f32 getFarZ() const {
        return mFarZ;
    }

    f32 getFovy() const {
        return mFovy;
    }

    Pos3f mView;
    Pos3f mViewInv;
    Proj3f mProjection;
    f32 mNearZ;
    f32 mFarZ;
    f32 mFovy;
    Vec2f mShakeOffset;
};
