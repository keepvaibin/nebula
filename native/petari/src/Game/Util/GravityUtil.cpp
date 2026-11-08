#include "Game/Util/GravityUtil.hpp"
#include "Game/Gravity/GravityInfo.hpp"
#include "Game/Gravity/PlanetGravity.hpp"
#include "Game/Gravity/PlanetGravityManager.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include <cstring>

namespace {
    char cRange[] = "Range";
    char cDistant[] = "Distant";
    char cPriority[] = "Priority";
    char cGravityId[] = "Gravity_id";
    char cGravityType[] = "Gravity_type";
    char cNormal[] = "Normal";
    char cShadow[] = "Shadow";
    char cMagnet[] = "Magnet";
    char cPower[] = "Power";
    char cLight[] = "Light";
    char cHeavy[] = "Heavy";
    char cInverse[] = "Inverse";

    PlanetGravityManager* getGravityManager() {
        return MR::getSceneObj< PlanetGravityManager >(SceneObj_PlanetGravityManager);
    }

    void getPlacementArgPlus(const TableIter& rIter, const char* pFieldName, f32* pDest) {
        f32 value;

        if (rIter.getValue(pFieldName, &value) && value >= 0.0f) {
            *pDest = value;
        }
    }

    void getPlacementArgPlus(const TableIter& rIter, const char* pFieldName, s32* pDest) {
        s32 value;

        if (rIter.getValue(pFieldName, &value) && value >= 0) {
            *pDest = value;
        }
    }

    bool calcGravityVectorOrZero(const NameObj* pActor, const Vec3f& rPosition, u32 typeFlags, Vec3f* pDest, GravityInfo* pInfo,
                                 uintptr_t host) {
        if (host == 0) {
            host = (uintptr_t)pActor;
        }

        return getGravityManager()->calcTotalGravityVector(pDest, pInfo, rPosition, typeFlags, host);
    }
};  // namespace

namespace MR {
    void registerGravity(PlanetGravity* pGravity) {
        ::getGravityManager()->registerGravity(pGravity);
    }

    bool calcGravityVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcGravityVector(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcDropShadowVector(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcGravityAndDropShadowVector(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Normal | GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcGravityAndMagnetVector(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Normal | GravityType_Magnet;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcGravityVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcGravityVectorOrZero(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Normal;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcDropShadowVectorOrZero(const NameObj* pActor, const Vec3f& rPosition, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, rPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcGravityAndDropShadowVectorOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_Normal | GravityType_Shadow;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, host);
    }

    bool calcAttractMarioLauncherOrZero(const LiveActor* pActor, Vec3f* pDest, GravityInfo* pInfo, uintptr_t host) {
        u32 typeFlags = GravityType_MarioLauncher;
        return ::calcGravityVectorOrZero(pActor, pActor->mPosition, typeFlags, pDest, pInfo, host);
    }

    bool isZeroGravity(const LiveActor* pActor) {
        Vec3f dummyGravity;
        return !::calcGravityVectorOrZero(pActor, pActor->mPosition, GravityType_Normal, &dummyGravity, nullptr, 0);
    }

    bool isLightGravity(const GravityInfo& rInfo) {
        PlanetGravity* pGravity = rInfo.mGravityInstance;

        if (pGravity == nullptr) {
            return false;
        }

        return pGravity->mGravityPower == GravityPower_Light;
    }

    void settingGravityParamFromPlacement(PlanetGravity* pGravity, const TableIter& rIter) {
        f32 range = pGravity->mRange;
        ::getPlacementArgPlus(rIter, ::cRange, &range);
        pGravity->mRange = range;

        f32 distant = pGravity->getDistant();
        ::getPlacementArgPlus(rIter, ::cDistant, &distant);
        pGravity->mDistant = distant;

        s32 priority = pGravity->mPriority;
        ::getPlacementArgPlus(rIter, ::cPriority, &priority);
        pGravity->setPriority(priority);

        s32 id = pGravity->mGravityId;
        ::getPlacementArgPlus(rIter, ::cGravityId, &id);
        pGravity->mGravityId = id;

        getPlacementGravityType(rIter, pGravity);
        getPlacementGravityPower(rIter, pGravity);

        s32 inverse = pGravity->mIsInverse;
        ::getPlacementArgPlus(rIter, ::cInverse, &inverse);
        pGravity->mIsInverse = inverse;
    }

    void getPlacementGravityType(const TableIter& rIter, PlanetGravity* pGravity) {
        const char* pType = nullptr;

        if (rIter.getValue(::cGravityType, &pType)) {
            if (strcmp(pType, ::cNormal) == 0) {
                pGravity->mGravityType = GravityType_Normal;
            } else if (strcmp(pType, ::cShadow) == 0) {
                pGravity->mGravityType = GravityType_Shadow;
            } else if (strcmp(pType, ::cMagnet) == 0) {
                pGravity->mGravityType = GravityType_Magnet;
            }
        }
    }

    void getPlacementGravityPower(const TableIter& rIter, PlanetGravity* pGravity) {
        const char* pPower = nullptr;

        if (rIter.getValue(::cPower, &pPower)) {
            if (strcmp(pPower, ::cLight) == 0) {
                pGravity->mGravityPower = GravityPower_Light;
            } else if (strcmp(pPower, ::cNormal) == 0) {
                pGravity->mGravityPower = GravityPower_Normal;
            } else if (strcmp(pPower, ::cHeavy) == 0) {
                pGravity->mGravityPower = GravityPower_Heavy;
            }
        }
    }
};  // namespace MR
