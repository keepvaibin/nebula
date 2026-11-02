#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry.hpp>

class CameraContext : public NameObj {
public:
    CameraContext();
    virtual ~CameraContext();

    void initParams();
    const TPos3f& getViewMtx() const;
    const TPos3f& getInvViewMtx() const;
    f32 getAspect() const;
    f32 getRenderFovy() const;
    void setViewMtx(const TPos3f&, bool, bool, const TVec3f&);
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

    TPos3f mView;
    TPos3f mViewInv;
    TProj3f mProjection;
    f32 mAspectRatio;
    f32 mNearZ;
    f32 mFarZ;
    f32 mFovy;
    TVec2f mShakeOffset;
};
