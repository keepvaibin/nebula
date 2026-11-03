#pragma once

#include <JSystem/JGeometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class BindSphere {
public:
    BindSphere(const TVec3f& rPosition, f32 radius);

    f32 getRadius() const {
        return mRadius;
    }

    TVec3f mPosition;
    f32 mRadius;
};

class BindCone {
public:
    BindCone(const TVec3f& rPosition, const TVec3f& rDirection, f32 height, f32 radius);

    bool isInPoint(const TVec3f& rPoint) const;
    void calcVertexPosition();
    void calcGeneratrixLength();
    void setPosition(const TVec3f& rPosition);
    void setDirection(const TVec3f& rDirection);

    f32 getRadius() const {
        return mRadius;
    }

    f32 getGeneratrixLength() const {
        return mGeneratrixLength;
    }

    TVec3f mPosition;
    TVec3f mVertex;
    TVec3f mDirection;
    f32 mHeight;
    f32 mRadius;
    f32 mGeneratrixLength;
};

class BindResult {
public:
    BindResult() : mPosition(0, 0, 0), mNormal(0, 0, 0), mContactPosition(0, 0, 0), mIsBound() {
    }

    TVec3f mPosition;
    TVec3f mNormal;
    TVec3f mContactPosition;
    bool mIsBound;
};

namespace MR {
    bool bindSpereToCone(BindResult* pResult, const TVec3f& rMovement, const BindSphere& rSphere, const BindCone& rCone);
    void updateBindPositionAndVelocity(TVec3f* pPosition, TVec3f* pVelocity, const BindResult& rResult, f32 factor);
};  // namespace MR
