#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class BezierSurface {
public:
    BezierSurface(s32, u32);

    void setST(const TVec2f&, const TVec2f&, const TVec2f&);
    void setDivideLevel(s32);
    void calcBuffer();
    void draw() const;
    void calcTrianglePatchVertix(TVec3f*) const;
    void calcTrianglePatchNormal(TVec3f*) const;
    void drawTrianglePatchPos() const;
    void drawTrianglePatchPosNorm() const;
    void drawTrianglePatchPosST() const;
    void drawTrianglePatchPosNormST() const;

    TVec3f mCtrlPts[13];
    TVec3f* mTrianglePatchVertices;  // array
    TVec3f* mTrianglePatchNormals;   // array
    TVec2f mSTPoints[3];
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
