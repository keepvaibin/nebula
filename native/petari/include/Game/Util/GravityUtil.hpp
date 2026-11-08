#pragma once

#include <nebula_geometry.hpp>

class GravityInfo;
class TableIter;
class LiveActor;
class NameObj;
class PlanetGravity;

namespace MR {
    void registerGravity(PlanetGravity* pGravity);
    bool calcGravityVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcGravityVector(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcDropShadowVector(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcGravityAndDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcGravityAndMagnetVector(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcGravityVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcGravityVectorOrZero(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcDropShadowVectorOrZero(const NameObj* pObj, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcGravityAndDropShadowVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool calcAttractMarioLauncherOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host);
    bool isZeroGravity(const LiveActor* pActor);
    bool isLightGravity(const GravityInfo& rInfo);
    void settingGravityParamFromPlacement(PlanetGravity* pGravity, const TableIter& rIter);
    void getPlacementGravityType(const TableIter& rIter, PlanetGravity* pGravity);
    void getPlacementGravityPower(const TableIter& rIter, PlanetGravity* pGravity);
};  // namespace MR
