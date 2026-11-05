#pragma once

#include <nebula_geometry.hpp>

class BezierSurface;

class OctahedronBezierSurface {
public:
    OctahedronBezierSurface(u32);

    virtual void movement();
    virtual void calcAnim();
    virtual void draw() const;

    const Vec3f& getVertex(s32) const;
    Vec3f* getVertexPtr(s32);
    void setDiagonalRate(f32);
    void setDivideLevel(s32);
    void calcControlPoint();

    Vec3f mVertices[6];  // top, right, back, left, front, bottom
    BezierSurface* mSurfaces[8];
    f32 mDiagonalRate;
};
