#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class JMapInfoIter;
class RailRider;
class ParabolicPath;

class SpinDriverShootPath {
public:
    SpinDriverShootPath();

    void init(const JMapInfoIter&);
    void initUsingParabolic(const JMapInfoIter&, const TVec3f&);
    void setStartPosition(const TVec3f&);
    void calcPosition(TVec3f*, f32) const;
    void calcDirection(TVec3f*, f32, f32) const;
    void calcInitPose(TVec3f*, TVec3f*, TVec3f*, f32) const;
    f32 getTotalLength() const;
    void calcClippingInfo(TVec3f*, f32*, f32, f32);

    RailRider* mRailRider;
    ParabolicPath* mPath;
    TVec3f mStartPosition;
    bool mUsesParabolic;
};
