#pragma once

#include <nebula_geometry.hpp>

class TableIter;
class RailRider;
class ParabolicPath;

class SpinDriverShootPath {
public:
    SpinDriverShootPath();

    void init(const TableIter&);
    void initUsingParabolic(const TableIter&, const Vec3f& rStartPos);
    void setStartPosition(const Vec3f& rStartPos);
    void calcPosition(Vec3f* pOutPosition, f32 rate) const;
    void calcDirection(Vec3f* pOutDirection, f32 rate, f32 step) const;
    void calcInitPose(Vec3f* pUp, Vec3f* pFront, Vec3f* pSide, f32 rollDegree) const;
    f32 getTotalLength() const;
    void calcClippingInfo(Vec3f* pCenter, f32* pRadius, f32 sampleInterval, f32 margin);

    RailRider* mRailRider;
    ParabolicPath* mPath;
    Vec3f mStartPosition;
    bool mUsesParabolic;
};
