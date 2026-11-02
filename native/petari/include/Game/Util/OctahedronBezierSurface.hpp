#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class BezierSurface;

class OctahedronBezierSurface {
public:
    OctahedronBezierSurface(u32);

    virtual void movement();
    virtual void calcAnim();
    virtual void draw() const;

    const TVec3f& getVertex(s32) const;
    TVec3f* getVertexPtr(s32);
    void setDiagonalRate(f32);
    void setDivideLevel(s32);
    void calcControlPoint();

    TVec3f mVertices[6];  // top, right, back, left, front, bottom
    BezierSurface* mSurfaces[8];
    f32 mDiagonalRate;
};
