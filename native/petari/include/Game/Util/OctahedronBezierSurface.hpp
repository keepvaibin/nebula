#pragma once

#include <nebula_geometry.hpp>

class BezierSurface;

class TypedDraw;

class OctahedronBezierSurface {
public:
    // flags: BezierSurface's, for each face.
    OctahedronBezierSurface(u32 flags);

    virtual void movement();
    virtual void calcAnim();
    void draw(TypedDraw& rDraw) const;

    const Vec3f& getVertex(s32) const;
    Vec3f* getVertexPtr(s32);
    void setDiagonalRate(f32);
    void setDivideLevel(s32);
    void calcControlPoint();

    Vec3f mVertices[6];  // top, right, back, left, front, bottom
    BezierSurface* mSurfaces[8];
    f32 mDiagonalRate;
};
