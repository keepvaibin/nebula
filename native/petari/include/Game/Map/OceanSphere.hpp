#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/TypedDraw.hpp"

class GameTexture;
class OceanSpherePoint;
class WaterInfo;

class OceanSpherePlane {
public:
    OceanSpherePlane(s32 pointCount, const Vec3f* pCenter, const Vec3f& rAxis1, const Vec3f& rAxis2, const Vec2f& rTex1, const Vec2f& rTex2,
                     const Vec2f& rTex3);

    void update(f32 radius, f32 wave1Time, f32 wave2Time);
    OceanSpherePoint* getPoint(int col, int row) const {
        return mPoints[(mAxisPointCount * row) + col];
    }

    s32 mGridPointCount;
    s32 mAxisPointCount;
    OceanSpherePoint** mPoints;
};

class OceanSpherePlaneEdge {
public:
    OceanSpherePlaneEdge(s32 pointCount, const Vec3f* pCenter, const Vec3f& rAxis1, const Vec3f& rAxis2, const Vec2f& rTex1, const Vec2f& rTex2);

    void update(f32 radius, f32 wave1Time, f32 wave2Time);

    s32 mPointCount;
    OceanSpherePoint** mPoints;
};

class OceanSphere : public LiveActor {
public:
    OceanSphere(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void control();
    virtual void draw() const;

    bool isInWater(const Vec3f& rPos) const;
    bool calcWaterInfo(const Vec3f& rPos, const Vec3f& rGravity, WaterInfo* pInfo) const;
    void exeWait();
    void exeRiseUp();
    void initPoints();
    void initMesh();
    void updatePoints();
    OceanSpherePoint* getPlanePointLeftUpper(int row, int col) const;
    OceanSpherePoint* getPlanePointRightUpper(int row, int col) const;
    OceanSpherePoint* getPlanePointLeftLower(int row, int col) const;
    OceanSpherePoint* getPlanePointRightLower(int row, int col) const;
    SiteMaterial loadMaterialFace() const;
    SiteMaterial loadMaterialBack() const;
    // Every corner of the sphere in order, through `begin(count)` for each strip.
    template < class T, class Begin >
    void writeSphere(Begin begin) const;
    void writeVertex(const OceanSpherePoint* pPoint, VertexPN*& rVtx) const;
    void writeVertex(const OceanSpherePoint* pPoint, VertexPNT2*& rVtx) const;
    void drawFace(TypedDraw& rDraw) const;
    void drawBack(TypedDraw& rDraw) const;

    f32 mRadius;
    f32 mRadiusTarget;
    s32 mPointCount;
    OceanSpherePoint* mAxisPointPX;
    OceanSpherePoint* mAxisPointNX;
    OceanSpherePoint* mAxisPointPY;
    OceanSpherePoint* mAxisPointNY;
    OceanSpherePoint* mAxisPointPZ;
    OceanSpherePoint* mAxisPointNZ;
    OceanSpherePlane* mPlaneLeftUpper;
    OceanSpherePlane* mPlaneRightUpper;
    OceanSpherePlane* mPlaneLeftLower;
    OceanSpherePlane* mPlaneRightLower;
    OceanSpherePlaneEdge* mEdge0;
    OceanSpherePlaneEdge* mEdge1;
    OceanSpherePlaneEdge* mEdge2;
    OceanSpherePlaneEdge* mEdge3;
    OceanSpherePlaneEdge* mEdge4;
    OceanSpherePlaneEdge* mEdge5;
    OceanSpherePlaneEdge* mEdge6;
    OceanSpherePlaneEdge* mEdge7;
    f32 mWaveTime1;
    f32 mWaveTime2;
    f32 mTexOffs0X;
    f32 mTexOffs0Y;
    f32 mTexOffs1X;
    f32 mTexOffs1Y;
    GameTexture* mWaterTex;
    GameTexture* mWaterEnvTex;
    NebulaColor8 mStageColor1Front;
    NebulaColor8 mStageColor1Back;
    bool mUseMesh;
    // The sphere's face and back as the renderer draws them.
    SiteMesh< VertexPNT2 > mMeshFace;
    SiteMesh< VertexPN > mMeshBack;
    bool mAlwaysUseRealDrawing;
    bool mIsCameraInside;
    bool mIsStartPosCamera;
    bool mEnableStartPosCameraSwitch;
};
