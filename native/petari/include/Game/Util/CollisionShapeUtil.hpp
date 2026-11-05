#pragma once

#include <nebula_geometry.hpp>

class HitSphere {
public:
    HitSphere() {
    }

    HitSphere(const Vec3f& rPos, f32 radius) {
        mPos.set(rPos);
        mRadius = radius;
    }

    void set(const Vec3f& rPos, f32 radius) {
        mPos.set(rPos);
        mRadius = radius;
    }

    bool isHit(const HitSphere& other) const {
        f32 rad = mRadius + other.mRadius;
        return mPos.squared(other.mPos) <= rad * rad;
    }

    Vec3f mPos;
    f32 mRadius;
};

class HitCylinder {
public:
    HitCylinder() {
    }

    HitCylinder(const Vec3f& rPos, const Vec3f& axis) {
        mPos.set(rPos);
        mAxis.set(axis);
    }

    void set(const Vec3f& rPos, const Vec3f& axis) {
        mPos.set(rPos);
        mAxis.set(axis);
    }

    void getParams(const Vec3f& rPos, f32* proj, f32* ortho) {
        Vec3f diff;
        diff.sub(rPos, mPos);
        f32 dist = diff.squared();
        f32 para = mAxis.squared();

        f32 dot = mAxis.dot(diff);

        *proj = dot / para;
        *ortho = FloatUtil::sqrt((para * dist - dot * dot) / para);
    }

    Vec3f mPos;
    Vec3f mAxis;
};
