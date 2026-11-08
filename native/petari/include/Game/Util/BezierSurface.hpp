#pragma once

#include <nebula_geometry.hpp>

class TypedDraw;

class BezierSurface {
public:
    enum {
        FLAG_NORMAL = 1 << 0,
        FLAG_TEX = 1 << 1,
        FLAG_CACHE = 1 << 2,  // calcBuffer keeps the patch (and its normals with FLAG_NORMAL); else each draw computes it
    };

    // Any flag: drawn with positions, normals and texture coordinates; none: positions only.
    BezierSurface(s32 divideLevel, u32 flags);

    void setST(const Vec2f&, const Vec2f&, const Vec2f&);
    void setDivideLevel(s32);
    void calcBuffer();
    void draw(TypedDraw& rDraw) const;
    void calcTrianglePatchVertix(Vec3f*) const;
    void calcTrianglePatchNormal(Vec3f*) const;
    void drawTrianglePatchPos(TypedDraw& rDraw) const;
    void drawTrianglePatchPosNormST(TypedDraw& rDraw) const;

    Vec3f mCtrlPts[13];
    Vec3f* mTrianglePatchVertices;  // array
    Vec3f* mTrianglePatchNormals;   // array
    Vec2f mSTPoints[3];
    s32 mDivideLevel;
    bool mIsLit;
};
