#pragma once

#include <nebula_geometry.hpp>

class JMapInfoIter;
class RailRider;
class ParabolicPath;

class SpinDriverShootPath {
public:
    SpinDriverShootPath();

    void init(const JMapInfoIter&);
    void initUsingParabolic(const JMapInfoIter&, const Vec3f&);
    void setStartPosition(const Vec3f&);
    void calcPosition(Vec3f*, f32) const;
    void calcDirection(Vec3f*, f32, f32) const;
    void calcInitPose(Vec3f*, Vec3f*, Vec3f*, f32) const;
    f32 getTotalLength() const;
    void calcClippingInfo(Vec3f*, f32*, f32, f32);

    RailRider* mRailRider;
    ParabolicPath* mPath;
    Vec3f mStartPosition;
    bool mUsesParabolic;
};
