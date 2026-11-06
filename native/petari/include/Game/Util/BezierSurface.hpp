#pragma once

#include <nebula_geometry.hpp>

class TypedDraw;

class BezierSurface {
public:
    BezierSurface(s32, u32);

    void setST(const Vec2f&, const Vec2f&, const Vec2f&);
    void setDivideLevel(s32);
    void calcBuffer();
    void draw(TypedDraw& rDraw) const;
    void calcTrianglePatchVertix(Vec3f*) const;
    void calcTrianglePatchNormal(Vec3f*) const;
    void drawTrianglePatchPos(TypedDraw& rDraw) const;
    void drawTrianglePatchPosNorm(TypedDraw& rDraw) const;
    void drawTrianglePatchPosST(TypedDraw& rDraw) const;
    void drawTrianglePatchPosNormST(TypedDraw& rDraw) const;

    Vec3f mCtrlPts[13];
    Vec3f* mTrianglePatchVertices;  // array
    Vec3f* mTrianglePatchNormals;   // array
    Vec2f mSTPoints[3];
    s32 mDivideLevel;
    s32 _C0;
    u32 _C4;
};

typedef f32 TrianglePatchCoefs[13];

struct TrianglePatch {
    TrianglePatchCoefs* mCoefTable;
    TrianglePatchCoefs* mCoefTangentSTable;
    TrianglePatchCoefs* mCoefTangentTTable;
    s32 mNumPoints;
    const u16* mTriangleStripOrder;
    u16 mNumTriangleStripPoints;
    const u16* mLineStripOrder;
    u16 mNumLineStripPoints;
    const Vec* mTriangleST;
};
