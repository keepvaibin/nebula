#pragma once

#include <nebula_geometry.hpp>

class ParabolicPath {
public:
    ParabolicPath();

    void initFromMaxHeight(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rPeak);
    void initFromUpVector(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rUp, f32 maxHeight);
    void initFromUpVectorAddHeight(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rUp, f32 height);
    // param runs 0 (start) to 1 (end).
    void calcPosition(Vec3f* pOutPosition, f32 param) const;
    void calcDirection(Vec3f* pOutDirection, f32 param, f32 step) const;
    f32 getLength(f32 startParam, f32 endParam, s32 numSegments) const;
    f32 getTotalLength(s32 numSegments) const;
    f32 calcPathSpeedFromAverageSpeed(f32 speed) const;

    Vec3f mPosition;
    Vec3f mAxisY;
    Vec3f mAxisZ;
    f32 mQuadCoef;
    f32 mLinearCoef;
    f32 mDistance;
};
