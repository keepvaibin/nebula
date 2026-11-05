#pragma once

#include <nebula_geometry.hpp>

class ParabolicPath {
public:
    ParabolicPath();

    void initFromMaxHeight(const Vec3f&, const Vec3f&, const Vec3f&);
    void initFromUpVector(const Vec3f&, const Vec3f&, const Vec3f&, f32);
    void initFromUpVectorAddHeight(const Vec3f&, const Vec3f&, const Vec3f&, f32);
    void calcPosition(Vec3f*, f32) const;
    void calcDirection(Vec3f*, f32, f32) const;
    f32 getLength(f32, f32, s32) const;
    f32 getTotalLength(s32) const;
    f32 calcPathSpeedFromAverageSpeed(f32) const;

    Vec3f mPosition;
    Vec3f mAxisY;
    Vec3f mAxisZ;
    f32 _24;
    f32 _28;
    f32 _2C;
};
