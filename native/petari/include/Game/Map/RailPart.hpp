#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class LinearRailPart;
class BezierRailPart;

class RailPart {
public:
    RailPart();

    void init(const TVec3f&, const TVec3f&, const TVec3f&, const TVec3f&);
    void initForBezier(const TVec3f&, const TVec3f&, const TVec3f&, const TVec3f&);
    void calcPos(TVec3f*, f32) const;
    void calcVelocity(TVec3f*, f32) const;
    f32 getLength(f32, f32, int) const;
    f32 getTotalLength() const;
    f32 getParam(f32) const;
    f32 getNearestParam(const TVec3f&, f32) const;

    LinearRailPart* mRailPartLinear;
    BezierRailPart* mRailPartBezier;
};

class LinearRailPart {
public:
    inline LinearRailPart() {
    }

    void set(const TVec3f&, const TVec3f&);

    f32 getNearestParam(const TVec3f&, f32) const;

    TVec3f mStart;        // start point of the rail
    TVec3f mCtrlDegree1;  // synonymous with "velocity" of the rail
    f32 mLength;          // length of the rail
};
