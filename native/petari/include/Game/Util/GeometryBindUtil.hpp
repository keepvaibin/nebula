#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class BindSphere {
public:
    BindSphere(const Vec3f& rPosition, f32 radius);

    f32 getRadius() const {
        return mRadius;
    }

    Vec3f mPosition;
    f32 mRadius;
};

class BindCone {
public:
    BindCone(const Vec3f& rPosition, const Vec3f& rDirection, f32 height, f32 radius);

    bool isInPoint(const Vec3f& rPoint) const;
    void calcVertexPosition();
    void calcGeneratrixLength();
    void setPosition(const Vec3f& rPosition);
    void setDirection(const Vec3f& rDirection);

    f32 getRadius() const {
        return mRadius;
    }

    f32 getGeneratrixLength() const {
        return mGeneratrixLength;
    }

    Vec3f mPosition;
    Vec3f mVertex;
    Vec3f mDirection;
    f32 mHeight;
    f32 mRadius;
    f32 mGeneratrixLength;
};

class BindResult {
public:
    BindResult() : mPosition(0, 0, 0), mNormal(0, 0, 0), mIsBound() {
    }

    Vec3f mPosition;
    Vec3f mNormal;
    bool mIsBound;
};

namespace MR {
    bool bindSpereToCone(BindResult* pResult, const Vec3f& rMovement, const BindSphere& rSphere, const BindCone& rCone);
    void updateBindPositionAndVelocity(Vec3f* pPosition, Vec3f* pVelocity, const BindResult& rResult, f32 factor);
};  // namespace MR
