#pragma once

#include "Game/LumaMarks.hpp"

#include "Game/Map/LightZoneDataHolder.hpp"
#include <nebula_geometry.hpp>

class AreaObj;
class AreaObjContainer;
class AreaObjMgr;
class AreaCubeBoundingBox;
class WaterInfo;

namespace MR {
    void calcSpherePos(Vec3f*, const AreaObj*);
    f32 getSphereRadius(const AreaObj*);
    AreaObjMgr* getAreaObjManager(const char*);
    AreaObj* getAreaObj(const char*, const Vec3f&);
    bool isInAreaObj(const char*, const Vec3f&);
    s32 getAreaObjArg(const AreaObj*, s32);
    void tryToUpdatePlayerRestartIdInfo(const Vec3f&);

    bool calcAreaMoveVelocity(Vec3f* pVelocity, const Vec3f& rPos);

    void calcCylinderPos(Vec3f*, const AreaObj*);
    void calcCylinderCenterPos(Vec3f*, const AreaObj*);

    void calcCylinderUpVec(Vec3f*, const AreaObj*);
    f32 getCylinderRadius(const AreaObj*);

    void calcCubeWorldBox(DirBox3f* pBox, const AreaObj* pArea);
    void calcCubePos(const AreaObj*, Vec3f*);
    void calcCubeAxisZ(const AreaObj*, Vec3f*);
    void calcCubeLocalPos(Vec3f*, const AreaObj*, const Vec3f&);

    void calcCubeRotate(const AreaObj*, LUMA_OUT Vec3f*);

    bool calcWhirlPoolAccelInfo(const Vec3f&, Vec3f*);

    Box3f* getCubeLocalBox(const AreaObj*);

    AreaObj* getCurrentAstroOverlookAreaObj();

    AreaObj* getAreaIn(const char*, const Vec3f&);

    bool getWaterAreaObj(WaterInfo*, const Vec3f&);
    bool getWaterAreaInfo(WaterInfo* pInfo, const Vec3f& rPos, const Vec3f& rGravity, bool skipConnectedArea);
};  // namespace MR
