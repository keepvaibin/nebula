#pragma once

#include <nebula_geometry.hpp>
#include "Game/LumaMarks.hpp"

class GravityInfo;
class TableIter;
class LiveActor;
class NameObj;
class PlanetGravity;

// pHost: gravities with this host are skipped; nullptr means the asking object.
namespace MR {
    void registerGravity(PlanetGravity* pGravity);
    bool calcGravityVector(const LiveActor* pActor, LUMA_OUT Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcGravityVector(const NameObj* pObj, const Vec3f& rPosition, LUMA_OUT Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcDropShadowVector(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcGravityAndDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcGravityAndMagnetVector(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcGravityVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcGravityVectorOrZero(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcDropShadowVectorOrZero(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcGravityAndDropShadowVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool calcAttractMarioLauncherOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, const void* pHost);
    bool isZeroGravity(const LiveActor* pActor);
    bool isLightGravity(const GravityInfo& rInfo);
    void settingGravityParamFromPlacement(PlanetGravity* pGravity, const TableIter& rIter);
    void getPlacementGravityType(const TableIter& rIter, PlanetGravity* pGravity);
    void getPlacementGravityPower(const TableIter& rIter, PlanetGravity* pGravity);
};  // namespace MR
