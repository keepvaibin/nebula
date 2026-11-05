#pragma once

#include <nebula_geometry.hpp>

class LinearRailPart;
class BezierRailPart;

class RailPart {
public:
    RailPart();

    void init(const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&);
    void initForBezier(const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&);
    void calcPos(Vec3f*, f32) const;
    void calcVelocity(Vec3f*, f32) const;
    f32 getLength(f32, f32, int) const;
    f32 getTotalLength() const;
    f32 getParam(f32) const;
    f32 getNearestParam(const Vec3f&, f32) const;

    LinearRailPart* mRailPartLinear;
    BezierRailPart* mRailPartBezier;
};

class LinearRailPart {
public:
    inline LinearRailPart() {
    }

    void set(const Vec3f&, const Vec3f&);

    f32 getNearestParam(const Vec3f&, f32) const;

    Vec3f mStart;        // start point of the rail
    Vec3f mCtrlDegree1;  // synonymous with "velocity" of the rail
    f32 mLength;          // length of the rail
};
